#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <pthread.h>

#include "libsvc/redblack.h"
#include "libsvc/http.h"
#include "libsvc/htsmsg_json.h"

#include "vimeo.h"


/**
 *
 */
RB_HEAD(state_entry_tree, state_entry);
typedef struct state_entry {
  RB_ENTRY(state_entry) se_entry;
  char *se_state;

  char *se_code;
} state_entry_t;

static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct state_entry_tree state_entries;


/**
 *
 */
static int
state_entry_cmp(const state_entry_t *a, const state_entry_t *b)
{
  return strcmp(a->se_state, b->se_state);
}


static int
vimeo_callback(http_connection_t *hc, const char *remain,
      void *opaque)
{
  state_entry_t *vce;
  static state_entry_t *skel;

  const char *referer = http_arg_get(&hc->hc_args, "referer");
  if(referer == NULL || strcmp(referer, "https://movian.tv/"))
    return 403;

  const char *state = http_arg_get(&hc->hc_req_args, "state");
  const char *code = http_arg_get(&hc->hc_req_args, "code");
  if(state == NULL || code == NULL)
    return 400;

  if(skel == NULL)
    skel = calloc(1, sizeof(state_entry_t));

  skel->se_state = strdup(state);
  skel->se_code = strdup(code);

  pthread_mutex_lock(&state_mutex);

  vce = RB_INSERT_SORTED(&state_entries, skel, se_entry, state_entry_cmp);
  if (vce != NULL)
    vce->se_code = strdup(code);
  else
    skel = NULL;

  pthread_mutex_unlock(&state_mutex);

  return 200;
}


static int
vimeo_code(http_connection_t *hc, const char *remain,
      void *opaque)
{
  state_entry_t *vce;
  static state_entry_t *skel;

  const char *referer = http_arg_get(&hc->hc_args, "referer");
  if(referer == NULL || strcmp(referer, "https://movian.tv/"))
    return 403;

  const char *state = http_arg_get(&hc->hc_req_args, "state");
  if(state == NULL)
    return 400;

  if(skel == NULL)
    skel = calloc(1, sizeof(state_entry_t));

  skel->se_state = (char *)state;

  pthread_mutex_lock(&state_mutex);

  vce = RB_FIND(&state_entries, skel, se_entry, state_entry_cmp);

  pthread_mutex_unlock(&state_mutex);

  if (vce == NULL)
    return 404;

  char *code = vce->se_code;

  htsmsg_t *msg = htsmsg_create_map();
  htsmsg_add_str(msg, "code", code);

  char *out = htsmsg_json_serialize_to_str(msg, 0);
  htsbuf_append(&hc->hc_reply, out, strlen(out));
  free(out);
  return http_send_reply(hc, 200, "application/json", NULL, NULL, 0);
}


void
vimeo_init(void)
{
  http_path_add("/vimeo/callback", NULL, vimeo_callback);
  http_path_add("/vimeo/code", NULL, vimeo_code);
}
