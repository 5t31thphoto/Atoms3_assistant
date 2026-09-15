# The fox firmware is one Arduino-as-component IDF app. All .cpp compile; the
# .inc files are #included by fox_main.cpp / fox_llm.cpp and must NOT be listed
# as sources. fox_sam.c is plain C.
idf_component_register(
    SRCS
        "fox_main.cpp"
        "fox_brain.cpp"
        "fox_voice.cpp"
        "fox_memory.cpp"
        "fox_llm.cpp"
        "fox_sam.c"
    INCLUDE_DIRS "."
)

# Embed nothing here; data partitions are flashed separately by CI/web tool.
