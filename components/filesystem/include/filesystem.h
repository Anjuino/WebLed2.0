#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include "esp_littlefs.h"
#include "freertos/FreeRTOS.h" 
#include "stdint.h"
#include "stdbool.h"
#include "esp_log.h"
#include <string>
#include "vector"

struct fileinfo_t {
  std::string name;
  uint16_t size;
};

class FileSystem{ 

  private:
    const char *TAG = "FileSystem";

    std::vector <fileinfo_t> filelist;
    esp_vfs_littlefs_conf_t conf;

    virtual void task();

  public:
    FileSystem() {};
    ~FileSystem() {};

    static void task_entry(void* pvParameters) { static_cast<FileSystem*>(pvParameters)->task(); };

    void init();
    std::string getfile(std::string name);   // получить файл по имени
    
    bool createfile(std::string name, std::string text);  //создать файл, если файл с таким именем уже есть, то он будет заменен

    bool deletefile(std::string name);

    bool exsistfile(std::string name);

    std::vector<fileinfo_t> getfilelist(); // вернуть текущий список файлов
    
    std::tuple<size_t, size_t> get_info();

};

#endif