#pragma once
#include "config_store.h"

void llm_client_init(const fox_config_t *cfg);

// High-level turn: audio already transcribed or text prompt
// Returns response text (caller frees if heap-allocated) or NULL on error
char *llm_client_chat(const char *user_text);

// Tool-calling loop helpers
void llm_client_register_tools(void);
