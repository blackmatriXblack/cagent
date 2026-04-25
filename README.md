# CAgent v1.0 – Technical Documentation

## 1. Project Overview

**CAgent** (formerly Terminal AI Assistant) is a lightweight, secure, command-line interface (CLI) agent written in C that bridges local terminal operations with the Qwen Large Language Model (LLM) via the DashScope API. Designed for developers and system administrators, it provides a unified interface for querying AI capabilities and executing a strictly whitelisted set of system commands.

The application emphasizes **security by design**, implementing a robust command whitelist to prevent arbitrary code execution while maintaining the utility of a local assistant. It features persistent API key management, pure ASCII output for maximum terminal compatibility, and modular architecture for easy extension.

### 1.1 Core Objectives
*   **Secure AI Integration:** Provide seamless access to Qwen's reasoning capabilities without exposing the system to unrestricted shell access.
*   **Controlled System Interaction:** Allow limited, safe system queries (e.g., `date`, `whoami`) through a strict whitelist mechanism.
*   **Persistent Configuration:** Securely store API credentials locally to streamline user experience across sessions.
*   **Cross-Platform Compatibility:** Ensure consistent behavior on Windows, Linux, and macOS using conditional compilation and standard C libraries.

### 1.2 Key Features
*   **Qwen API Bridge:** Direct integration with Alibaba Cloud's DashScope API for high-quality natural language processing.
*   **Whitelisted Command Execution:** A security-first approach that only allows predefined safe commands (`ls`, `pwd`, `date`, etc.), blocking all others.
*   **Secure Key Management:** API keys are stored in a local configuration file (`.ai_agent_config`) with restricted permissions on Unix-like systems.
*   **Pure ASCII Interface:** Avoids UTF-8 encoding issues, ensuring readability in any terminal environment.
*   **Modular Design:** Separates API logic, security checks, and UI handling for maintainability.

---

## 2. System Architecture

### 2.1 Security Model: The Whitelist Engine
The core security feature is the `safe_execute_command` function. Instead of passing user input directly to `system()`, the input is validated against a hardcoded array of allowed prefixes.

```c
const char *whitelist[] = {
    "date ", "time ", "whoami ", "pwd ", "ls ", "dir ",
    "echo ", "cat README ", "help ", "hostname ", "uptime "
};
```
*   **Prefix Matching:** Uses `strstr(command, whitelist[i]) == command` to ensure the command *starts* with an allowed string.
*   **Blocking:** Any command not matching the whitelist is rejected with a `[SECURITY BLOCKED]` message.
*   **Rationale:** This prevents dangerous operations like `rm -rf /`, `format C:`, or reverse shells, while still allowing useful informational queries.

### 2.2 API Integration Layer
The application uses `libcurl` for HTTP requests and `json-c` for parsing responses.

*   **Request Construction:** Builds a JSON payload conforming to the DashScope API specification.
*   **Authentication:** Injects the API key into the `Authorization: Bearer <key>` header.
*   **Response Parsing:** Extracts the `content` field from the nested JSON response structure (`output.choices[0].message.content`).

### 2.3 Data Persistence
*   **Configuration File:** `.ai_agent_config` stores the API key in plain text.
*   **Permission Hardening:** On non-Windows systems, `chmod(CONFIG_FILE, S_IRUSR | S_IWUSR)` ensures only the owner can read/write the key file.

---

## 3. Module Detailed Analysis

### 3.1 Main Control Loop (`main`)

The main function implements a standard REPL (Read-Eval-Print Loop):

1.  **Initialization:** Loads the API key from `.ai_agent_config`. If missing, prompts the user to run `setup`.
2.  **Input Parsing:** Uses `sscanf(input, "%63s %[^\n]", command, args)` to split the input into a command verb and its arguments.
3.  **Dispatch:**
    *   `setup`: Triggers the API key configuration wizard.
    *   `ask <question>`: Calls `call_qwen_api(args)`.
    *   `run <command>`: Calls `safe_execute_command(args)`.
    *   `help`/`exit`: Standard utility functions.

### 3.2 API Key Management (`setup_api_key`, `save_api_key`)

*   **Input Masking:** On Linux/macOS, `system("stty -echo")` is used to hide the API key during entry. On Windows, it relies on standard `fgets` (note: Windows console masking is more complex and often requires WinAPI).
*   **Validation:** Checks if the key length is at least 30 characters to prevent saving empty or invalid strings.
*   **Storage:** Writes the key to `.ai_agent_config` and immediately restricts file permissions.

### 3.3 Qwen API Caller (`call_qwen_api`)

This function handles the network interaction:

1.  **Payload Generation:**
    ```c
    snprintf(json_payload, sizeof(json_payload),
        "{\"model\":\"qwen-plus\",\"input\":{\"messages\":[ "
        "{\"role\":\"system\",\"content\":\"You are a helpful assistant.\"}, "
        "{\"role\":\"user\",\"content\":\"%s\"} ]}}", user_prompt);
    ```
2.  **Curl Execution:**
    *   Sets `CURLOPT_URL` to the DashScope endpoint.
    *   Attaches JSON headers and Authorization bearer token.
    *   Uses a custom write callback (`curl_write_callback`) to accumulate the response in memory.
3.  **JSON Parsing:**
    *   Uses `json_tokener_parse` to convert the raw string into a JSON object.
    *   Navigates the object tree to find the response content.
    *   Prints the result or an error if parsing fails.

### 3.4 Safe Command Executor (`safe_execute_command`)

*   **Iteration:** Loops through the `whitelist` array.
*   **Match Check:** If a match is found, it prints `[EXECUTING SAFE COMMAND]` and calls `system(command)`.
*   **Failure:** If no match is found, it prints `[SECURITY BLOCKED]` and lists allowed commands.

---

## 4. Compilation and Deployment

### 4.1 Prerequisites
*   **Compiler:** GCC, Clang, or MSVC.
*   **Libraries:**
    *   `libcurl`: For HTTP requests.
    *   `json-c`: For JSON parsing.
*   **OS:** Linux, macOS, or Windows.

### 4.2 Build Instructions

**On Linux/macOS:**
```bash
gcc main.c -o cagent -lcurl -ljson-c
./cagent
```

**On Windows (MinGW):**
```cmd
gcc main.c -o cagent.exe -lcurl -ljson-c
cagent.exe
```

**On Windows (MSVC):**
*Note: You must link against `libcurl.lib` and `json-c.lib`.*
```cmd
cl main.c libcurl.lib json-c.lib
cagent.exe
```

### 4.3 Conditional Compilation
The code uses `#ifdef HAVE_CURL` to handle environments where `libcurl` is not installed. If compiled without curl support, the `ask` command will print an error message instructing the user to install the libraries.

---

## 5. Security Considerations

### 5.1 Strengths
1.  **Command Whitelisting:** Prevents arbitrary command injection. Even if a user tries `run rm -rf /`, it will be blocked because `rm` is not in the whitelist.
2.  **File Permissions:** Restricts access to the API key file on Unix systems.
3.  **Input Buffering:** Uses `fgets` and fixed-size buffers to prevent buffer overflows.

### 5.2 Limitations & Risks
1.  **Plain Text Key Storage:** The API key is stored in plain text. While permissions are restricted, a root user or malware with sufficient privileges could still read it.
2.  **Whitelist Rigidity:** The whitelist is hardcoded. Adding new commands requires recompiling the application.
3.  **Windows Echo Masking:** The current implementation does not mask input on Windows, potentially exposing the API key in the console history.
4.  **No HTTPS Certificate Pinning:** Relies on default `libcurl` SSL verification. In high-security environments, certificate pinning should be added.

### 5.3 Recommendations
*   **Environment Variables:** Consider supporting API keys via environment variables (`QWEN_API_KEY`) to avoid file storage entirely.
*   **Dynamic Whitelist:** Allow users to add trusted commands to a configuration file after authentication.
*   **Input Sanitization:** Further sanitize `args` to prevent shell metacharacter exploitation within allowed commands (e.g., `ls; rm -rf /`).

---

## 6. Future Roadmap

### 6.1 Short-Term Enhancements
*   **History Feature:** Save previous questions and answers to a local log file.
*   **Context Awareness:** Maintain a conversation history array to allow follow-up questions (multi-turn dialogue).
*   **Better Windows Support:** Implement proper input masking using Windows API (`GetConsoleMode`/`SetConsoleMode`).

### 6.2 Long-Term Vision
*   **Plugin System:** Allow dynamic loading of "safe" modules for extended functionality (e.g., git status, docker ps).
*   **GUI Frontend:** Port to a lightweight GUI using GTK or Qt for a more user-friendly experience.
*   **Local LLM Support:** Integrate with `llama.cpp` to allow offline AI queries without API keys.

---

## 7. Conclusion

**CAgent v1.0** is a well-structured, security-conscious tool that demonstrates how to safely integrate LLMs into system workflows. By prioritizing a whitelist-based execution model and secure key management, it provides a reliable bridge between human intent and AI capability. While currently limited to a specific set of system commands, its modular design makes it an excellent foundation for more advanced AI-driven terminal agents.
