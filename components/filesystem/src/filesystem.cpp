#include "filesystem.h"
#include <dirent.h>
#include <sys/stat.h>


void FileSystem::init()
{
  conf = {
    .base_path = "/littlefs",
    .partition_label = "storage",
    .format_if_mount_failed = true,
    .dont_mount = false,
  };

  esp_err_t ret = esp_vfs_littlefs_register(&conf);

  if (ret != ESP_OK)
  {
    if (ret == ESP_FAIL)
    {
      ESP_LOGE(TAG, "Failed to mount or format filesystem");
    }
    else if (ret == ESP_ERR_NOT_FOUND)
    {
      ESP_LOGE(TAG, "Failed to find LittleFS partition");
    }
    else
    {
      ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
    }
    return;
  }


  auto [total, used] = get_info();
  ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
}

std::tuple<size_t, size_t> FileSystem::get_info()
{
  size_t total = 0, used = 0;
  esp_err_t ret = esp_littlefs_info(conf.partition_label, &total, &used);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to get LittleFS partition information (%s)", esp_err_to_name(ret));
    return {-1, -1};
  }

  return {total, used};
}

void FileSystem::task()
{ 
  init();

  vTaskDelay(pdMS_TO_TICKS(1000));
  std::vector list = getfilelist();

  uint8_t count = 0;
  for(auto item: list) {
    count++;
    ESP_LOGI(TAG, "list: name: %s, size: %u", item.name.c_str(), item.size);
  }

  ESP_LOGI(TAG, "file count %d", count);

  vTaskDelay(pdMS_TO_TICKS(5000));
  while(true) {
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

std::vector<fileinfo_t> FileSystem::getfilelist()
{
  filelist.clear();
  
  DIR *dir = opendir(conf.base_path);
  if (dir == nullptr)
  {
      ESP_LOGE(TAG, "Failed to open directory: %s", conf.base_path);
      return filelist;
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != nullptr)
  {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;

    if (entry->d_type == DT_DIR) continue;

    fileinfo_t info;
    info.name = entry->d_name;

    std::string fullpath = std::string(conf.base_path) + "/" + entry->d_name;
    struct stat st;
    if (stat(fullpath.c_str(), &st) == 0)
    {
      info.size = static_cast<uint16_t>(st.st_size);
    }
    else
    {
      info.size = 0;
      ESP_LOGW(TAG, "Failed to stat file: %s", fullpath.c_str());
    }

    filelist.push_back(info);
  }

  closedir(dir);
  return filelist;
}

bool FileSystem::deletefile(std::string name)
{
  std::string fullpath = std::string(conf.base_path) + "/" + name;

  if (remove(fullpath.c_str()) != 0)
  {
    ESP_LOGE(TAG, "Failed to delete file: %s (errno=%d)", fullpath.c_str(), errno);
    return false;
  }

  ESP_LOGI(TAG, "Deleted: %s", name.c_str());
  return true;
}

bool FileSystem::createfile(std::string name, std::string text)
{
  std::string fullpath = std::string(conf.base_path) + "/" + name;

  FILE *file = fopen(fullpath.c_str(), "w");
  if (!file) {
    ESP_LOGE(TAG, "Failed to open file for write: %s (errno=%d)", fullpath.c_str(), errno);
    return false;
  }

  size_t written = fwrite(text.data(), 1, text.size(), file);
  bool ret = (written == text.size());

  if (fclose(file) != 0) {
    ESP_LOGE(TAG, "fclose failed: %s (errno=%d)", fullpath.c_str(), errno);
    ret = false;
  }

  if (!ret) {
    ESP_LOGE(TAG, "Write incomplete: %s (%zu/%zu bytes)", fullpath.c_str(), written, text.size());
  }
  return ret;
}

std::string FileSystem::getfile(std::string name)
{
  std::string fullpath = std::string(conf.base_path) + "/" + name;

  FILE *file = fopen(fullpath.c_str(), "r");
  if (!file) {
    ESP_LOGE(TAG, "Failed to open file: %s (errno=%d)", fullpath.c_str(), errno);
    return {};
  }

  std::string text;
  char buf[128];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), file)) > 0) {
    text.append(buf, n);
  }

  if (ferror(file)) {
    ESP_LOGE(TAG, "Read error: %s (errno=%d)", fullpath.c_str(), errno);
  }

  fclose(file);
  return text;
}

bool FileSystem::exsistfile(std::string name)
{
  std::string fullpath = std::string(conf.base_path) + "/" + name;

  struct stat st;
  if (stat(fullpath.c_str(), &st) == 0) {
    return true;
  }
  return false;
}