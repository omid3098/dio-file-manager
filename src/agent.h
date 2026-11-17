#ifndef DFM_AGENT_H
#define DFM_AGENT_H
/* The agent base: uses a coding agent that is already installed (claude, codex, gemini, ollama). No CUI here.
   - Nothing runs at start. agent_detect() looks at PATH and the usual install folders; the UI calls it when a feature is
     first used and each time the picker opens.
   - One choice (agent + model) is kept in %APPDATA%\dio-file-manager\agent.txt.
   - A job runs the agent's non-interactive mode in one hidden process (no console window), with the prompt on its
     standard input and its answer read from standard output. The agent is asked to read only; the app makes every change.
   UTF-8 everywhere. */
#include <stdbool.h>
#include <stddef.h>

enum { AGENT_MAX_MODELS = 5, AGENT_RUNNING = 0, AGENT_DONE = 1, AGENT_FAILED = 2, AGENT_TIMEOUT = 3, AGENT_CANCELLED = 4 };

int agent_real_count(void);                  /* the four real agents */
int agent_count(void);                       /* the known agents, in menu order (the test agent is last, only in a check) */
const char *agent_id(int a);                 /* "claude" */
const char *agent_title(int a);              /* "Claude Code" */
const char *agent_get_url(int a);            /* where to get it */
int agent_model_count(int a);
const char *agent_model_title(int a, int m); /* m 0 is the agent's own default */
bool agent_found(int a);
void agent_detect(void);                     /* look again: PATH and install folders; cheap */
int agent_found_count(void);

bool agent_choice(int *a, int *m);           /* the saved choice; false while none (or its agent is gone) */
void agent_choice_set(int a, int m);         /* saves it */
void agent_choice_text(char *out, size_t cap);   /* "Claude Code \xC2\xB7 Sonnet"; "" without a choice */

typedef struct agent_job agent_job;
/* workdir: the folder the agent may read. timeout_ms: the whole run. NULL sets nothing; the caller reports. */
agent_job *agent_start(const char *prompt, const char *workdir, unsigned timeout_ms);
int agent_poll(agent_job *j, const char **text, double *seconds);   /* text: the output once it is not running */
void agent_cancel(agent_job *j);
void agent_set_notify(void *window);        /* the window to wake (HWND) when a job ends, so the UI need not poll often */
void agent_free(agent_job *j);               /* cancels, kills the process and joins the thread */

/* Names. */
typedef struct agent_pair { char old_name[300], new_name[300]; } agent_pair;
int agent_parse_names(const char *text, agent_pair *out, int cap);   /* {"names":[{"old":..,"new":..}]}; tolerates fences and chatter */
/* The suggestion made safe: leaf only, characters Windows refuses replaced, trailing dots and spaces cut, device names
   avoided, the extension of old_name kept. False when nothing is left or the name is the old one. */
bool agent_clean_name(const char *old_name, const char *suggested, char *out, size_t cap);
bool agent_looks_machine_made(const char *name);   /* IMG_1234.jpg, a8f3c2e1.pdf, Untitled, download (3) */
/* Builds the prompt for files in one folder: names (and a text snippet when the file is plain text). */
size_t agent_build_prompt(const char *dir, const char *const *names, int n, char *out, size_t cap);

/* Checks only (the window passes them from --test-state): DFM_TEST_AGENT is the command line of a stand-in agent,
   DFM_TEST_NO_AGENTS hides the real ones. */
unsigned agent_timeout_ms(void);             /* how long one run may take: 3 minutes; a check can shorten it */
void agent_test_timeout(unsigned ms);
void agent_test_hooks(const char *stub_command, bool hide_real);
#endif
