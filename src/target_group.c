#include "../include/target_group.h"

/**
 * @brief pointer to handle the circular linked list
 */
struct target_group_list_node *target_groups_head = NULL;

/**
 * @brief mutex for thead safety
 */
pthread_mutex_t target_group_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief insert the target group in the linked list following the order of
 * the priority. (1 is higher than 5)
 *
 * @param target_group
 */
void target_group_list_sorted_insert(struct target_group target_group) {
    struct target_group_list_node *new_target_group_node =
        (struct target_group_list_node *)malloc(
            sizeof(struct target_group_list_node));
    new_target_group_node->tg = target_group;

    struct target_group_list_node *temp;

    if (target_groups_head == NULL ||
        target_groups_head->tg.priority >= target_group.priority) {
        new_target_group_node->next = target_groups_head;
        target_groups_head = new_target_group_node;
    } else {
        temp = target_groups_head;
        while (temp->next != NULL &&
               temp->next->tg.priority < target_group.priority) {
            temp = temp->next;
        }
        new_target_group_node->next = temp->next;
        temp->next = new_target_group_node;
    }
}

/**
 * @brief regex match the incoming path with all the targer groups and
 * return the matched one. if not found, return the default one. if no default
 * is set, return the topmost priority one
 *
 * @param path
 * @param tg
 */
void find_target_group_with_path(char *path, struct target_group **tg) {
    pthread_mutex_lock(&target_group_mutex);

    struct target_group_list_node *temp = target_groups_head;
    struct target_group *default_tg = &target_groups_head->tg;

    regex_t path_regex;
    int regex_result;

    while (temp != NULL) {
        if (temp->tg.is_default == 1) {  // found the default!
            // extracting out the default one while iterating the list!
            default_tg = &temp->tg;
        }

        regcomp(&path_regex, temp->tg.path, 0);
        regex_result = regexec(&path_regex, path, 0, NULL, 0);
        if (regex_result == 0) {
            pthread_mutex_unlock(&target_group_mutex);
            *tg = &temp->tg;
            return;
        }

        temp = temp->next;
    }

    pthread_mutex_unlock(&target_group_mutex);
    *tg = default_tg;
}

/**
 * @brief check health of all the targets
 *
 */
void health_check_all_target_groups() {
    // logger("Health check started");

    struct target_group_list_node *temp = target_groups_head;
    while (temp != NULL) {
        health_check_all_targets(&temp->tg.round_robin_head,
                                 temp->tg.round_robin_mutex);
        temp = temp->next;
    }

    // logger("Health check ended");
}

/**
 * @brief the loop what checks the health of all target groups in
 * regular intervals
 *
 * @param arg
 * @return void*
 */
void *passive_health_check(void *arg) {
    while (1) {
        sleep(HEALTH_CHECK_INTERVAL);
        health_check_all_target_groups();
    }
}

/**
 * @brief standalone thread for the passive health check
 *
 */
void build_passive_health_check_thread() {
    pthread_t passive_health_check_thread;
    pthread_create(&passive_health_check_thread, NULL, &passive_health_check,
                   NULL);
}

/**
 * @brief get the health json response
 *
 * @param json
 */
void get_health_json(char *json) {
    pthread_mutex_lock(&target_group_mutex);

    struct target_group_list_node *temp = target_groups_head;

    char tg_health[20480], t_health[20480], rr_health[20480],
        tg_temp_health[20480];

    strcpy(tg_health, "");
    while (temp != NULL) {
        stpcpy(tg_temp_health, "");
        sprintf(tg_temp_health,
                "{ \"path\": \"%s\", \"priority\": %d, \"default\": %s, "
                "\"targets\": ",
                temp->tg.path, temp->tg.priority,
                temp->tg.is_default == 1 ? "true" : "false");
        strcat(tg_health, tg_temp_health);

        struct round_robin_node *rr_temp = temp->tg.round_robin_head;
        strcpy(t_health, "[ ");

        while (rr_temp->next != temp->tg.round_robin_head) {
            strcpy(rr_health, "");
            sprintf(rr_health, "{ \"name\": \"%s\", \"status\": \"%s\" }",
                    rr_temp->backend.name,
                    rr_temp->backend.is_healthy == 1 ? "up" : "down");
            strcat(t_health, rr_health);
            strcat(t_health, ", ");
            rr_temp = rr_temp->next;
        }
        strcpy(rr_health, "");
        sprintf(rr_health, "{ \"name\": \"%s\", \"status\": \"%s\" }",
                rr_temp->backend.name,
                rr_temp->backend.is_healthy == 1 ? "up" : "down");
        strcat(t_health, rr_health);

        strcat(t_health, " ]");
        strcat(tg_health, t_health);
        strcat(tg_health, " }, ");
        temp = temp->next;
    }

    tg_health[strlen(tg_health) - 2] = '\0';
    strcpy(json, "");
    sprintf(json, "{ \"target_groups\": [ %s ] }", tg_health);

    pthread_mutex_unlock(&target_group_mutex);
}