#include <windows.h>
#include <commdlg.h>

#include <string>
#include <vector>

namespace {

std::wstring Utf8ToWide(const char *value) {
    if (value == nullptr || *value == '\0') {
        return {};
    }

    int length = MultiByteToWideChar(CP_UTF8, 0, value, -1, nullptr, 0);
    if (length <= 0) {
        return {};
    }

    std::wstring result(length - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value, -1, result.data(), length);
    return result;
}

std::string WideToUtf8(const wchar_t *value) {
    if (value == nullptr || *value == L'\0') {
        return {};
    }

    int length = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }

    std::string result(length - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), length, nullptr, nullptr);
    return result;
}

} // namespace

extern "C" char *tinyfd_saveFileDialog(
    char const *title,
    char const *defaultPathAndOrFile,
    int numOfFilterPatterns,
    char const *const *filterPatterns,
    char const *singleFilterDescription) {
    std::wstring filter = Utf8ToWide(singleFilterDescription);
    if (filter.empty()) {
        filter = L"All files";
    }
    filter.push_back(L'\0');

    if (numOfFilterPatterns > 0 && filterPatterns != nullptr) {
        for (int index = 0; index < numOfFilterPatterns; ++index) {
            if (filterPatterns[index] != nullptr) {
                if (index > 0) {
                    filter.push_back(L';');
                }
                filter += Utf8ToWide(filterPatterns[index]);
            }
        }
    } else {
        filter += L"*.*";
    }
    filter.push_back(L'\0');
    filter.push_back(L'\0');

    std::wstring fileName = Utf8ToWide(defaultPathAndOrFile);
    if (fileName.empty()) {
        fileName = L"untitled";
    }
    fileName.resize(32768, L'\0');

    std::wstring dialogTitle = Utf8ToWide(title);

    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrTitle = dialogTitle.c_str();
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrFile = fileName.data();
    dialog.nMaxFile = static_cast<DWORD>(fileName.size());
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&dialog)) {
        return nullptr;
    }

    static std::string selectedPath;
    selectedPath = WideToUtf8(dialog.lpstrFile);
    return selectedPath.empty() ? nullptr : selectedPath.data();
}

extern "C" char *tinyfd_openFileDialog(
    char const *title,
    char const *defaultPathAndOrFile,
    int numOfFilterPatterns,
    char const *const *filterPatterns,
    char const *singleFilterDescription,
    int allowMultipleSelects) {
    std::wstring filter = Utf8ToWide(singleFilterDescription);
    if (filter.empty()) {
        filter = L"All files";
    }
    filter.push_back(L'\0');

    if (numOfFilterPatterns > 0 && filterPatterns != nullptr) {
        for (int index = 0; index < numOfFilterPatterns; ++index) {
            if (filterPatterns[index] != nullptr) {
                if (index > 0) {
                    filter.push_back(L';');
                }
                filter += Utf8ToWide(filterPatterns[index]);
            }
        }
    } else {
        filter += L"*.*";
    }
    filter.push_back(L'\0');
    filter.push_back(L'\0');

    std::wstring fileName = Utf8ToWide(defaultPathAndOrFile);
    fileName.resize(32768, L'\0');

    std::wstring dialogTitle = Utf8ToWide(title);

    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrTitle = dialogTitle.c_str();
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrFile = fileName.data();
    dialog.nMaxFile = static_cast<DWORD>(fileName.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (allowMultipleSelects) {
        dialog.Flags |= OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    }

    if (!GetOpenFileNameW(&dialog)) {
        return nullptr;
    }

    static std::string selectedPath;
    selectedPath = WideToUtf8(dialog.lpstrFile);
    return selectedPath.empty() ? nullptr : selectedPath.data();
}