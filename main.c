/*
 * Terminal AI Assistant - Pure ASCII Version
 * No UTF-8 characters to avoid terminal encoding issues
 * Connects to Qwen API via DashScope
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#ifdef _WIN32
    #include <windows.h>
    #define OS_WINDOWS 1
#else
    #define OS_WINDOWS 0
#endif

#define CONFIG_FILE ".ai_agent_config"
#define MAX_INPUT 4096
#define MAX_API_KEY 128

char api_key[MAX_API_KEY] = {0};
int api_key_loaded = 0;

// ============================================================================
// Security: Whitelisted safe commands only
// ============================================================================
int safe_execute_command(const char *command) {
    if (!command || strlen(command) == 0) return -1;

    // ONLY allow these safe commands
    const char *whitelist[] = {
        "date", "time", "whoami", "pwd", "ls", "dir",
        "echo", "cat README", "help", "hostname", "uptime"
    };
    int whitelist_size = sizeof(whitelist) / sizeof(whitelist[0]);

    for (int i = 0; i < whitelist_size; i++) {
        if (strstr(command, whitelist[i]) == command) {
            printf("\n[EXECUTING SAFE COMMAND]: %s\n", command);
            return system(command);
        }
    }

    printf("\n[SECURITY BLOCKED] Command not in whitelist: '%s'\n", command);
    printf("Allowed commands:\n");
    for (int i = 0; i < whitelist_size; i++) {
        printf("  - %s\n", whitelist[i]);
    }
    return -1;
}

// ============================================================================
// API Key Management
// ============================================================================
int load_api_key() {
    FILE *fp = fopen(CONFIG_FILE, "r");
    if (!fp) return 0;

    if (fgets(api_key, MAX_API_KEY, fp)) {
        size_t len = strlen(api_key);
        if (len > 0 && api_key[len-1] == '\n') api_key[len-1] = '\0';
        api_key_loaded = 1;
    }
    fclose(fp);
    return api_key_loaded;
}

void save_api_key(const char *key) {
    FILE *fp = fopen(CONFIG_FILE, "w");
    if (!fp) {
        fprintf(stderr, "ERROR: Cannot save API key (permission denied)\n");
        return;
    }
    fprintf(fp, "%s", key);
    fclose(fp);

#ifndef _WIN32
    chmod(CONFIG_FILE, S_IRUSR | S_IWUSR);
#endif

    strncpy(api_key, key, MAX_API_KEY-1);
    api_key_loaded = 1;
    printf("\n[OK] API key saved securely to %s\n", CONFIG_FILE);
}

// ============================================================================
// Qwen API Integration (requires libcurl + json-c)
// ============================================================================
#ifdef HAVE_CURL
#include <curl/curl.h>
#include <json-c/json.h>

struct curl_response {
    char *data;
    size_t size;
};

size_t curl_write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct curl_response *mem = (struct curl_response *)userp;

    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) return 0;

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;

    return realsize;
}

int call_qwen_api(const char *user_prompt) {
    if (!api_key_loaded || strlen(api_key) == 0) {
        printf("[ERROR] API key not configured. Type 'setup' first.\n");
        return -1;
    }

    CURL *curl;
    CURLcode res;
    struct curl_response chunk = {0};
    chunk.data = malloc(1);
    chunk.size = 0;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if (curl) {
        char json_payload[MAX_INPUT * 2];
        snprintf(json_payload, sizeof(json_payload),
            "{\"model\":\"qwen-plus\",\"input\":{\"messages\":["
            "{\"role\":\"system\",\"content\":\"You are a helpful assistant.\"},"
            "{\"role\":\"user\",\"content\":\"%s\"}"
            "]}}", user_prompt);

        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
        headers = curl_slist_append(headers, auth_header);

        curl_easy_setopt(curl, CURLOPT_URL, "https://dashscope.aliyuncs.com/api/v1/services/aigc/text-generation/generation");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

        printf("\n[CONNECTING] Sending request to Qwen API...\n");
        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            fprintf(stderr, "[ERROR] API request failed: %s\n", curl_easy_strerror(res));
        } else {
            struct json_object *parsed_json = json_tokener_parse(chunk.data);
            if (parsed_json) {
                struct json_object *output, *choices, *message, *content;
                if (json_object_object_get_ex(parsed_json, "output", &output) &&
                    json_object_object_get_ex(output, "choices", &choices) &&
                    json_array_get_length(choices) > 0) {

                    struct json_object *first_choice = json_array_get_idx(choices, 0);
                    if (json_object_object_get_ex(first_choice, "message", &message) &&
                        json_object_object_get_ex(message, "content", &content)) {

                        printf("\n[QWEN RESPONSE]\n%s\n\n", json_object_get_string(content));
                    }
                }
                json_object_put(parsed_json);
            } else {
                printf("[WARNING] Could not parse API response\n");
            }
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }

    free(chunk.data);
    curl_global_cleanup();
    return 0;
}
#else
int call_qwen_api(const char *user_prompt) {
    printf("[INFO] Qwen API requires libcurl and json-c libraries.\n");
    printf("Your question: \"%s\"\n", user_prompt);
    printf("To enable API support, compile with:\n");
    printf("  gcc -o agent agent.c -lcurl -ljson-c\n\n");
    return -1;
}
#endif

// ============================================================================
// Main Interface (Pure ASCII)
// ============================================================================
void show_help() {
    printf("\n");
    printf("===============================================================\n");
    printf("        TERMINAL AI ASSISTANT (C Language - ASCII Only)       \n");
    printf("===============================================================\n");
    printf("Commands:\n");
    printf("  setup        - Enter/Update Qwen API key\n");
    printf("  ask <text>   - Send question to Qwen AI\n");
    printf("  run <cmd>    - Execute SAFE whitelisted command\n");
    printf("  help         - Show this help menu\n");
    printf("  exit         - Quit the program\n");
    printf("---------------------------------------------------------------\n");
    printf("SECURITY NOTICE:\n");
    printf("  * This tool CANNOT control all applications\n");
    printf("  * System commands are RESTRICTED to safe whitelist\n");
    printf("  * API key stored locally in .ai_agent_config\n");
    printf("===============================================================\n\n");
}

void setup_api_key() {
    char input[MAX_API_KEY * 2];
    printf("\n[SETUP] Enter your Qwen API Key from https://dashscope.aliyuncs.com\n");
    printf("Key: ");

#ifndef _WIN32
    system("stty -echo");
    fgets(input, sizeof(input), stdin);
    system("stty echo");
    printf("\n");
#else
    fgets(input, sizeof(input), stdin);
#endif

    size_t len = strlen(input);
    if (len > 0 && input[len-1] == '\n') input[len-1] = '\0';

    if (strlen(input) < 30) {
        printf("[ERROR] Invalid API key (too short). Minimum 30 characters required.\n");
        return;
    }

    save_api_key(input);
    printf("[OK] API key configured successfully!\n");
}

int main() {
    char input[MAX_INPUT];
    char command[64], args[MAX_INPUT];

    printf("===============================================================\n");
    printf("      WELCOME TO TERMINAL AI ASSISTANT (C Language)          \n");
    printf("===============================================================\n");

    if (load_api_key()) {
        printf("[OK] API key loaded from %s\n", CONFIG_FILE);
    } else {
        printf("[INFO] No API key found. Type 'setup' to configure Qwen access.\n");
    }

    show_help();

    while (1) {
        printf("\nAI-Agent> ");
        if (!fgets(input, sizeof(input), stdin)) break;

        size_t len = strlen(input);
        if (len > 0 && input[len-1] == '\n') input[len-1] = '\0';
        if (len == 1) continue;

        if (sscanf(input, "%63s %[^\n]", command, args) < 1) continue;

        if (strcmp(command, "exit") == 0 || strcmp(command, "quit") == 0) {
            printf("[GOODBYE] Exiting Terminal AI Assistant.\n");
            break;
        }
        else if (strcmp(command, "help") == 0) {
            show_help();
        }
        else if (strcmp(command, "setup") == 0) {
            setup_api_key();
        }
        else if (strcmp(command, "ask") == 0) {
            if (strlen(args) == 0) {
                printf("[ERROR] Usage: ask <your question>\n");
                continue;
            }
            call_qwen_api(args);
        }
        else if (strcmp(command, "run") == 0) {
            if (strlen(args) == 0) {
                printf("[ERROR] Usage: run <command>\n");
                continue;
            }
            safe_execute_command(args);
        }
        else {
            printf("[ERROR] Unknown command '%s'. Type 'help' for commands.\n", command);
        }
    }

    return 0;
}