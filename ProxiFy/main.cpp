#include <iostream>
#include <Windows.h>
#include <imagehlp.h>
#include <vector>
#include <string>
#include <fstream>

using namespace std;

// Check if its 32bit or 64bit
WORD fileType;

// Exported names
vector<string> names;

vector<string> Explode(const string& s, const char& c) {
  string buff;
  vector<string> v;

  for (auto n : s) {
    if (n != c) buff += n;
    else
      if (n == c && !buff.empty()) {
        v.push_back(buff);
        buff = "";
      }
  }
  if (!buff.empty()) v.push_back(buff);

  return v;
}

bool GetImageFileHeaders(const string& fileName, IMAGE_NT_HEADERS& headers) {
  const auto fileHandle = CreateFileA(
    fileName.c_str(),
    GENERIC_READ,
    FILE_SHARE_READ,
    nullptr,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    nullptr
  );
  if (fileHandle == INVALID_HANDLE_VALUE)
    return false;

  const auto imageHandle = CreateFileMappingA(
    fileHandle,
    nullptr,
    PAGE_READONLY,
    0,
    0,
    nullptr
  );
  if (imageHandle == nullptr) {
    CloseHandle(fileHandle);
    return false;
  }

  const auto imagePtr = MapViewOfFile(
    imageHandle,
    FILE_MAP_READ,
    0,
    0,
    0
  );
  if (imagePtr == nullptr) {
    CloseHandle(imageHandle);
    CloseHandle(fileHandle);
    return false;
  }

  const auto headersPtr = ImageNtHeader(imagePtr);
  if (headersPtr == nullptr) {
    UnmapViewOfFile(imagePtr);
    CloseHandle(imageHandle);
    CloseHandle(fileHandle);
    return false;
  }

  headers = *headersPtr;

  UnmapViewOfFile(imagePtr);
  CloseHandle(imageHandle);
  CloseHandle(fileHandle);

  return true;
}

void ListDllFunctions(const string& sADllName, vector<string>& slListOfDllFunctions) {
  unsigned long cDirSize;
  _LOADED_IMAGE LoadedImage{};
  slListOfDllFunctions.clear();
  if (MapAndLoad(sADllName.c_str(), nullptr, &LoadedImage, TRUE, TRUE)) {


    const auto imageExportDirectory = static_cast<_IMAGE_EXPORT_DIRECTORY*>(ImageDirectoryEntryToData(
      LoadedImage.MappedAddress,
      false, IMAGE_DIRECTORY_ENTRY_EXPORT, &cDirSize));

    if (imageExportDirectory != nullptr) {
      const auto dNameRVAs = static_cast<DWORD *>(ImageRvaToVa(LoadedImage.FileHeader,
                                                               LoadedImage.MappedAddress,
                                                               imageExportDirectory->AddressOfNames, nullptr));

      for (size_t i = 0; i < imageExportDirectory->NumberOfNames; i++) {
        string sName = static_cast<char *>(ImageRvaToVa(LoadedImage.FileHeader,
                                                        LoadedImage.MappedAddress,
                                                        dNameRVAs[i], nullptr));

        slListOfDllFunctions.push_back(sName);
      }

    }
    UnMapAndLoad(&LoadedImage);
  }
}

void GenerateDef(const string& name, vector<string> names) {
  std::fstream file;
  file.open(name + ".def", std::ios::out);
  file << "LIBRARY " << name << endl;
  file << "EXPORTS" << endl;

  // Loop them
  for (auto i = 0; i < names.size(); i++) {
    file << "\t" << names[i] << "=PROXY_" << names[i] << " @" << i + 1 << endl;
  }

  file.close();
}

void GenerateMainCpp(const string& name, vector<string> names) {
  std::fstream file;
  file.open(name + ".cpp", std::ios::out);
  file << "#include <windows.h>" << endl << endl;

  file << "HINSTANCE hLThis = 0;" << endl;
  file << "FARPROC p[" << names.size() << "];" << endl;
  file << "HINSTANCE hL = 0;" << endl << endl;


  file << "BOOL WINAPI DllMain(HINSTANCE hInst,DWORD reason,LPVOID)" << endl;
  file << "{" << endl;
  file << "\tif (reason == DLL_PROCESS_ATTACH)" << endl;
  file << "\t{" << endl;
  file << "\t\thLThis = hInst;" << endl;
  file << "\t\thL = LoadLibrary(\".\\\\" << name << "_.dll\");" << endl;
  file << "\t\tif(!hL) return false;" << endl;
  file << "\t}" << endl << endl;

  // Exports addresses
  for (auto i = 0; i < names.size(); i++) {
    file << "\tp[" << i << "] = GetProcAddress(hL, \"" << names[i] << "\");" << endl;
  }


  file << "\tif (reason == DLL_PROCESS_DETACH)" << endl;
  file << "\t{" << endl;
  file << "\t\tFreeLibrary(hL);" << endl;
  file << "\t\treturn 1;" << endl;

  file << "\t}" << endl << endl;;
  file << "\treturn 1;" << endl;
  file << "}" << endl << endl;


  // Generate Exports
  file << "extern \"C\"" << endl << "{" << endl;

  if (fileType == IMAGE_FILE_MACHINE_AMD64) {
    file << "\tFARPROC PA = NULL;" << endl;
    file << "\tint RunASM();" << endl << endl;

    for (auto i = 0; i < names.size(); i++) {
      file << "\tvoid " << "PROXY_" << names[i] << "() {" << endl;
      file << "\t\tPA = p[" << i << "];" << endl;
      file << "\t\tRunASM();" << endl;
      file << "\t}" << endl;
    }
  }
  else {
    for (auto i = 0; i < names.size(); i++) {
      file << "\tvoid " << "PROXY_" << names[i] << "() {" << endl;
      file << "\t\t__asm" << endl << "\t\t {";
      file << "\t\t\tjmp p[" << i + 1 << " * 4]" << endl;
      file << "\t\t}" << endl;
      file << "\t}" << endl;
    }
  }

  file << "}" << endl;

  file.close();
}

void GenerateAsm(const string& name) {
  std::fstream file;
  file.open(name + ".asm", std::ios::out);
  file << ".data" << endl;
  file << "extern PA : qword" << endl;
  file << ".code" << endl;
  file << "RunASM proc" << endl;
  file << "jmp qword ptr [PA]" << endl;
  file << "RunASM endp" << endl;
  file << "end" << endl;

  file.close();
}

int main() {
  OPENFILENAMEA ofn;
  char szFile[100];

  // open a file name
  ZeroMemory(&ofn, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = nullptr;
  ofn.lpstrFile = szFile;
  ofn.lpstrFile[0] = '\0';
  ofn.nMaxFile = sizeof(szFile);
  ofn.lpstrFilter = "DLL\0*.dll\0";
  ofn.nFilterIndex = 1;
  ofn.lpstrFileTitle = nullptr;
  ofn.nMaxFileTitle = 0;
  ofn.lpstrInitialDir = nullptr;
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

  cout << "ProxiFy - Copyright (C) Kristoffer Blasiak." << endl;
  cout << "Select the DLL you want to make a proxy for." << endl;

  if (!GetOpenFileNameA(&ofn)) {
    MessageBoxA(nullptr, "You have to choose a file.", "File not opened", MB_OK);
    return 0;
  }


  IMAGE_NT_HEADERS headers;
  if (GetImageFileHeaders(ofn.lpstrFile, headers)) {
    fileType = headers.FileHeader.Machine;
  }

  if (fileType == IMAGE_FILE_MACHINE_AMD64)
    MessageBoxA(nullptr, "64 bit file", "64 bit file", MB_OK);

  if (fileType == IMAGE_FILE_MACHINE_I386)
    MessageBoxA(nullptr, "32 bit file", "32 bit file", MB_OK);

  // Get filename
  vector<std::string> fileNameV = Explode(ofn.lpstrFile, '\\');
  std::string fileName = fileNameV[fileNameV.size() - 1];
  fileName = fileName.substr(0, fileName.size() - 4);

  // Get dll export names
  ListDllFunctions(ofn.lpstrFile, names);


  // Create Def File
  GenerateDef(fileName, names);
  GenerateMainCpp(fileName, names);

  if (fileType == IMAGE_FILE_MACHINE_AMD64)
    GenerateAsm(fileName);


  return 0;
}
