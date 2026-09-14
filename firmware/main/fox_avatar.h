#pragma once

void fox_avatar_init(const char *name, const char *primary, const char *accent);
void fox_avatar_show_splash(const char *text);
void fox_avatar_set_emotion(const char *emotion); // "idle","talk","happy","think","radar",...
void fox_avatar_set_mouth(float open);            // 0.0 .. 1.0 for lip-sync
void fox_avatar_tick(void);                       // call from display task
