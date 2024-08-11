#include "edfs.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>

#include "net.h"
#include "panic.h"

#define EDFS_CLUSTER_MAX 1024 /* Can be increased up to UINT16_MAX. */
#define EDFS_PATH83_MAX  260  /* In DOS, with limited 8.3 filenames. */
#define EDFS_PATH_MAX    512  /* In Linux with possibly longer filenames. */

#define EDFS_RMDIR       0x01
#define EDFS_MKDIR       0x03
#define EDFS_CHDIR       0x05
#define EDFS_CLOSEFILE   0x06
#define EDFS_READFILE    0x08
#define EDFS_WRITEFILE   0x09
#define EDFS_LOCK        0x0A
#define EDFS_UNLOCK      0x0B
#define EDFS_DISKSPACE   0x0C
#define EDFS_SETATTR     0x0E
#define EDFS_GETATTR     0x0F
#define EDFS_RENAME      0x11
#define EDFS_DELETE      0x13
#define EDFS_OPEN        0x16
#define EDFS_CREATE      0x17
#define EDFS_FINDFIRST   0x1B
#define EDFS_FINDNEXT    0x1C
#define EDFS_SEEKFROMEND 0x21
#define EDFS_SPOPNFIL    0x2E

#define EDFS_RESULT_OK               0x00
#define EDFS_RESULT_INVALID_FUNCTION 0x01
#define EDFS_RESULT_FILE_NOT_FOUND   0x02
#define EDFS_RESULT_PATH_NOT_FOUND   0x03
#define EDFS_RESULT_ACCESS_DENIED    0x05
#define EDFS_RESULT_NO_MORE_MATCH    0x12

typedef struct edfs_cluster_s {
  char path[EDFS_PATH_MAX];
  char path83[EDFS_PATH83_MAX];
} edfs_cluster_t;

#define EDFS_TRACE_BUFFER_SIZE 2048
#define EDFS_TRACE_MAX 256

static char edfs_trace_buffer[EDFS_TRACE_BUFFER_SIZE][EDFS_TRACE_MAX];
static int edfs_trace_buffer_n = 0;

static bool edfs_inited = false;
static char edfs_root[EDFS_PATH_MAX];
static edfs_cluster_t edfs_cluster[EDFS_CLUSTER_MAX];
static uint16_t edfs_cluster_used = 0;



static void edfs_trace(const char *format, ...)
{
  va_list args;

  va_start(args, format);
  vsnprintf(edfs_trace_buffer[edfs_trace_buffer_n],
    EDFS_TRACE_MAX, format, args);
  va_end(args);

  edfs_trace_buffer_n++;
  if (edfs_trace_buffer_n >= EDFS_TRACE_BUFFER_SIZE) {
    edfs_trace_buffer_n = 0;
  }
}



static char *unixpath_to_path83(const char *in, char *out)
{
  const char *p;
  int n = 0;
  bool in_ext = false;
  int count_8 = 0;
  int count_3 = 0;

  for (p = in; *p != '\0'; p++) {
    if (*p == '/') {
      out[n] = '\\';
      n++;
      in_ext = false;
      count_8 = 0;
      count_3 = 0;

    } else if (*p == '.') {
      if (in_ext) {
        count_3++;
        if (count_3 > 3) {
          continue;
        }
        out[n] = '.';
        n++;
      } else {
        out[n] = '.';
        n++;
        in_ext = true;
      }

    } else if (isprint(*p)) {
      if (in_ext) {
        count_3++;
        if (count_3 > 3) {
          continue;
        }
      } else {
        count_8++;
        if (count_8 > 8) {
          continue;
        }
      }
      out[n] = toupper(*p);
      n++;
    }
  }
  out[n] = '\0';

  return out;
}



static char *path83_to_filefcb(const char *in, char *out)
{
  const char *p;
  int n = 0;
  bool in_ext = false;

  /* Prepare output string: */
  for (n = 0; n < 11; n++) {
    out[n] = ' ';
  }
  out[11] = '\0';

  /* Special cases for current and parent directory: */
  if (strncmp(in, ".", 2) == 0) {
    out[0] = '.';
    return out;
  } else if (strncmp(in, "..", 3) == 0) {
    out[0] = '.';
    out[1] = '.';
    return out;
  }

  /* Convert from 8.3 to FCB format: */
  p = strrchr(in, '\\');
  if (p == NULL) {
    p = in; /* Assume no directory part in path. */
  } else {
    p++;
  }

  n = 0;
  while (*p != '\0') {
    if ((*p == '.') && (in_ext == false)) {
      in_ext = true;
      n = 8;
      p++;
      continue;
    }
    out[n] = *p;
    p++;
    n++;
    if (n >= 11) {
      break;
    }
  }

  return out;
}



static char *path83_dirname(const char *in, char *out)
{
  char *p;

  strncpy(out, in, EDFS_PATH83_MAX);
  p = strrchr(out, '\\');
  if (p != NULL) {
    *p = '\0';
  }

  return out;
}



static uint16_t time_to_dos_time(time_t sec)
{
  struct tm tm;
  uint16_t dos_time;

  localtime_r(&sec, &tm);
  dos_time  = tm.tm_sec / 2;
  dos_time += tm.tm_min << 5;
  dos_time += tm.tm_hour << 11;

  return dos_time;
}



static uint16_t time_to_dos_date(time_t sec)
{
  struct tm tm;
  uint16_t dos_date;

  localtime_r(&sec, &tm);
  dos_date  = tm.tm_mday;
  dos_date += (tm.tm_mon + 1) << 5;
  dos_date += (tm.tm_year - 80) << 9;

  return dos_date;
}



static uint16_t bsd_checksum(uint8_t buffer[], size_t len)
{
  size_t i;
  uint32_t checksum = 0;

  for (i = 0; i < len; i++) {
    checksum  = (checksum >> 1) + ((checksum & 1) << 15);
    checksum += buffer[i];
    checksum &= 0xFFFF;
  }

  return checksum;
}



static void edfs_cluster_register(const char *path, const char *path83)
{
  uint16_t cluster;

  /* If path already exists, then don't register again: */
  for (cluster = 0; cluster < edfs_cluster_used; cluster++) {
    if ((strlen(edfs_cluster[cluster].path) == strlen(path)) &&
        (strncmp(edfs_cluster[cluster].path, path, strlen(path)) == 0)) {
      return;
    }
  }

  /* Bail out if limit has been reached: */
  if (edfs_cluster_used >= EDFS_CLUSTER_MAX) {
    panic("No more EtherDFS clusters available!\n");
    return;
  }

  /* Register new: */
  cluster = edfs_cluster_used;
  strncpy(edfs_cluster[cluster].path, path, EDFS_PATH_MAX);
  strncpy(edfs_cluster[cluster].path83, path83, EDFS_PATH83_MAX);
  edfs_cluster_used++;

  edfs_trace(" register: 0x%04x -> '%s' -> '%s'\n",
    cluster, path, path83);
}



static void edfs_cluster_unregister(const char *path)
{
  uint16_t cluster;

  for (cluster = 0; cluster < edfs_cluster_used; cluster++) {
    if ((strlen(edfs_cluster[cluster].path) == strlen(path)) &&
        (strncmp(edfs_cluster[cluster].path, path, strlen(path)) == 0)) {
      edfs_trace(" unregister: 0x%04x -> '%s'\n", cluster,
        edfs_cluster[cluster].path);
      edfs_cluster[cluster].path[0] = '\0';
      edfs_cluster[cluster].path83[0] = '\0';
      return;
    }
  }

  edfs_trace(" unregister: '%s' (not found)\n", path);
}



static char *edfs_cluster_lookup(uint16_t cluster)
{
  if (cluster < EDFS_CLUSTER_MAX) {
    if (cluster >= edfs_cluster_used) {
      edfs_trace(" lookup: 0x%04x (not found)\n", cluster);
      return NULL;
    } else {
      edfs_trace(" lookup: 0x%04x -> '%s'\n", cluster,
        edfs_cluster[cluster].path);
      return edfs_cluster[cluster].path;
    }
  } else {
    edfs_trace(" lookup: 0x%04x (out of bounds)\n", cluster);
    return NULL;
  }
}



static char *edfs_cluster_lookup_83(const char *path83, uint16_t *cluster_out)
{
  uint16_t cluster;

  if (strlen(path83) == 0) {
    edfs_trace(" lookup: '' -> '' (root)\n");
    *cluster_out = 0;
    return "";
  }

  if (strncmp(path83, "\\", 2) == 0) {
    edfs_trace(" lookup: '\\' -> '' (root)\n");
    *cluster_out = 0;
    return "";
  }

  for (cluster = 0; cluster < edfs_cluster_used; cluster++) {
    if ((strlen(edfs_cluster[cluster].path83) == strlen(path83)) &&
        (strncmp(edfs_cluster[cluster].path83, path83, strlen(path83)) == 0)) {
      edfs_trace(" lookup: '%s' -> '%s'\n", path83,
        edfs_cluster[cluster].path);
      *cluster_out = cluster;
      return edfs_cluster[cluster].path;
    }
  }

  edfs_trace(" lookup: '%s' (not found)\n", path83);
  return NULL;
}



static char *path83_to_unixpath(const char *in, char *out)
{
  char dir83[EDFS_PATH83_MAX];
  uint16_t cluster;
  const char *src;
  char *p;
  int n;

  path83_dirname(in, dir83);
  p = edfs_cluster_lookup_83(dir83, &cluster);
  if (p != NULL) {
    /* Ideally use the correctly mapped directory and convert only basename. */
    strncpy(out, p, EDFS_PATH_MAX);
    src = in + strlen(dir83);
    n = strlen(out);
  } else {
    /* Fallback is to convert everything. */
    src = in;
    n = 0;
  }

  for (; *src != '\0'; src++) {
    if (*src == '\\') {
      out[n] = '/';
      n++;
    } else if (isprint(*src)) {
      out[n] = *src;
      n++;
    }
  }
  out[n] = '\0';

  return out;
}



void edfs_init(const char *root)
{
  int i;

  strncpy(edfs_root, root, EDFS_PATH_MAX);

  for (i = 0; i < EDFS_CLUSTER_MAX; i++) {
    edfs_cluster[i].path[0] = '\0';
    edfs_cluster[i].path83[0] = '\0';
  }
  edfs_cluster_register("", ""); /* Register root directory as cluster 0. */

  for (i = 0; i < EDFS_TRACE_BUFFER_SIZE; i++) {
    edfs_trace_buffer[i][0] = '\0';
  }
  edfs_trace_buffer_n = 0;

  edfs_inited = true;
}



static char *edfs_path(uint8_t tx_frame[], uint16_t tx_len, uint16_t offset,
  char *path)
{
  if ((tx_len - offset) >= PATH_MAX) {
    return NULL;
  }
  path[tx_len - offset] = '\0';
  strncpy(path, (char *)&tx_frame[offset], tx_len - offset);
  return path;
}



static bool edfs_match(const char filefcb[], const char pattern[])
{
  int i;

  for (i = 0; i < 11; i++) {
    if (pattern[i] == '\0') {
      break;
    }
    if (filefcb[i] == '\0') {
      break;
    }
    if (pattern[i] != '?') {
      if (pattern[i] != filefcb[i]) {
        return false;
      }
    }
  }

  return true;
}



static void edfs_set_result(uint8_t rx_frame[], uint16_t code)
{
  edfs_trace(" result: 0x%02x\n", code);
  rx_frame[0x3A] = code & 0xFF;
  rx_frame[0x3B] = code >> 8;
}



static uint16_t edfs_find(uint8_t rx_frame[], uint8_t attrib, char *pattern,
  uint16_t cluster, uint16_t target_pos)
{
  DIR *dh;
  struct dirent *entry;
  struct stat st;
  char *p;
  char tmp_path[PATH_MAX];
  char path[EDFS_PATH_MAX];
  char path83[EDFS_PATH83_MAX];
  char filefcb[12];
  uint16_t pos;
  uint16_t dos_date;
  uint16_t dos_time;

  if (attrib == 0x08) { /* Volume */
    edfs_set_result(rx_frame, EDFS_RESULT_NO_MORE_MATCH);
    return 0x3C;
  }

  if (target_pos == 0) { /* FINDFIRST */
    path83_dirname(pattern, path83);
    p = edfs_cluster_lookup_83(path83, &cluster);
    if (p == NULL) {
      edfs_set_result(rx_frame, EDFS_RESULT_PATH_NOT_FOUND);
      return 0x3C;
    }
    strncpy(path, p, EDFS_PATH_MAX);

  } else { /* FINDNEXT */
    p = edfs_cluster_lookup(cluster);
    if (p == NULL) {
      edfs_set_result(rx_frame, EDFS_RESULT_PATH_NOT_FOUND);
      return 0x3C;
    }
    strncpy(path, p, EDFS_PATH_MAX);
  }

  snprintf(tmp_path, PATH_MAX, "%s/%s", edfs_root, path);
  dh = opendir(tmp_path);
  if (dh == NULL) {
    edfs_set_result(rx_frame, EDFS_RESULT_PATH_NOT_FOUND);
    return 0x3C;
  }

  pos = 0;
  while ((entry = readdir(dh))) {
    /* Handling of hidden entries: */
    if (strncmp(entry->d_name, ".",  1) == 0) {
      if ((strncmp(entry->d_name, ".",  2) == 0) ||
          (strncmp(entry->d_name, "..", 3) == 0)) {
        if (strlen(path) == 0) {
          continue; /* Skip "." and ".." in root directory only. */
        }
      } else {
        continue; /* Skip all other hidden files. */
      }
    }

    snprintf(tmp_path, PATH_MAX, "%s/%s/%s", edfs_root, path, entry->d_name);
    if (stat(tmp_path, &st) != 0) {
      edfs_set_result(rx_frame, EDFS_RESULT_PATH_NOT_FOUND);
      return 0x3C;
    }

    if (S_ISDIR(st.st_mode)) {
      pos++;
      if ((attrib & 0x10) != 0x10) {
        continue;
      }
      rx_frame[0x3C] = 0x10; /* Directory */

    } else if (S_ISREG(st.st_mode)) {
      pos++;
      rx_frame[0x3C] = 0x00; /* File */

    } else {
      continue; /* Not a file or directory. */
    }

    /* Register all valid entries: */
    snprintf(tmp_path, PATH_MAX, "%s/%s", path, entry->d_name);
    unixpath_to_path83(tmp_path, path83);
    edfs_cluster_register(tmp_path, path83);

    unixpath_to_path83(entry->d_name, path83);
    path83_to_filefcb(path83, filefcb);

    if (target_pos == 0) { /* FINDFIRST */
      if (edfs_match(filefcb, path83_to_filefcb(pattern, tmp_path)) == false) {
        continue;
      }
    } else { /* FINDNEXT */
      if (edfs_match(filefcb, pattern) == false) {
        continue;
      }
    }

    dos_time = time_to_dos_time(st.st_mtime);
    dos_date = time_to_dos_date(st.st_mtime);

    rx_frame[0x3D] = filefcb[0];
    rx_frame[0x3E] = filefcb[1];
    rx_frame[0x3F] = filefcb[2];
    rx_frame[0x40] = filefcb[3];
    rx_frame[0x41] = filefcb[4];
    rx_frame[0x42] = filefcb[5];
    rx_frame[0x43] = filefcb[6];
    rx_frame[0x44] = filefcb[7];
    rx_frame[0x45] = filefcb[8];
    rx_frame[0x46] = filefcb[9];
    rx_frame[0x47] = filefcb[10];
    rx_frame[0x48] = dos_time & 0xFF;
    rx_frame[0x49] = dos_time >> 8;
    rx_frame[0x4A] = dos_date & 0xFF;
    rx_frame[0x4B] = dos_date >> 8;
    rx_frame[0x4C] = st.st_size & 0xFF;
    rx_frame[0x4D] = st.st_size >> 8;
    rx_frame[0x4E] = st.st_size >> 16;
    rx_frame[0x4F] = st.st_size >> 24;
    rx_frame[0x50] = cluster & 0xFF;
    rx_frame[0x51] = cluster >> 8;
    rx_frame[0x52] = pos & 0xFF;
    rx_frame[0x53] = pos >> 8;

    if (target_pos == 0 || (target_pos + 1) == pos) {
      edfs_trace(" find: 0x%04x -> '%s'\n", cluster, filefcb);
      closedir(dh);
      edfs_set_result(rx_frame, EDFS_RESULT_OK);
      return 0x54;
    }
  }

  edfs_trace(" find: (not found)'\n");
  closedir(dh);
  edfs_set_result(rx_frame, EDFS_RESULT_NO_MORE_MATCH);
  return 0x3C;
}



static uint16_t edfs_open(uint8_t rx_frame[], char *path83,
  uint16_t attrib, uint16_t action)
{
  FILE *fh;
  struct stat st;
  char *p;
  char tmp_path[PATH_MAX];
  char path[EDFS_PATH_MAX];
  char filefcb[12];
  uint16_t cluster;
  uint16_t dos_date;
  uint16_t dos_time;

  p = edfs_cluster_lookup_83(path83, &cluster);
  if (p == NULL) {
    if ((action & 0xF0) == 0x10) { /* Create if it does not exist. */
      path83_to_unixpath(path83, path);
      snprintf(tmp_path, PATH_MAX, "%s/%s", edfs_root, path);
      fh = fopen(tmp_path, "w+b"); /* Open and close the file to create it. */
      if (fh == NULL) {
        edfs_set_result(rx_frame, EDFS_RESULT_PATH_NOT_FOUND);
        return 0x3C;
      }
      fclose(fh);
      edfs_cluster_register(path, path83);
      p = edfs_cluster_lookup_83(path83, &cluster);
      if (p == NULL) {
        edfs_set_result(rx_frame, EDFS_RESULT_PATH_NOT_FOUND);
        return 0x3C;
      }
    } else { /* No action to create, so fail. */
      edfs_set_result(rx_frame, EDFS_RESULT_FILE_NOT_FOUND);
      return 0x3C;
    }
  } else {
    if ((action & 0x0F) == 0x02) { /* Exists, but truncate the file. */
      path83_to_unixpath(path83, path);
      snprintf(tmp_path, PATH_MAX, "%s/%s", edfs_root, path);
      fh = fopen(tmp_path, "w+b"); /* Open and close the file to truncate. */
      if (fh == NULL) {
        edfs_set_result(rx_frame, EDFS_RESULT_PATH_NOT_FOUND);
        return 0x3C;
      }
      fclose(fh);
    }
  }

  snprintf(tmp_path, PATH_MAX, "%s/%s", edfs_root, p);
  if (stat(tmp_path, &st) != 0) {
    edfs_set_result(rx_frame, EDFS_RESULT_PATH_NOT_FOUND);
    return 0x3C;
  }

  if (! S_ISREG(st.st_mode)) { /* Only operate on files. */
    edfs_set_result(rx_frame, EDFS_RESULT_FILE_NOT_FOUND);
    return 0x3C;
  }

  path83_to_filefcb(path83, filefcb);

  dos_time = time_to_dos_time(st.st_mtime);
  dos_date = time_to_dos_date(st.st_mtime);

  rx_frame[0x3C] = 0x00; /* File */
  rx_frame[0x3D] = filefcb[0];
  rx_frame[0x3E] = filefcb[1];
  rx_frame[0x3F] = filefcb[2];
  rx_frame[0x40] = filefcb[3];
  rx_frame[0x41] = filefcb[4];
  rx_frame[0x42] = filefcb[5];
  rx_frame[0x43] = filefcb[6];
  rx_frame[0x44] = filefcb[7];
  rx_frame[0x45] = filefcb[8];
  rx_frame[0x46] = filefcb[9];
  rx_frame[0x47] = filefcb[10];
  rx_frame[0x48] = dos_time & 0xFF;
  rx_frame[0x49] = dos_time >> 8;
  rx_frame[0x4A] = dos_date & 0xFF;
  rx_frame[0x4B] = dos_date >> 8;
  rx_frame[0x4C] = st.st_size & 0xFF;
  rx_frame[0x4D] = st.st_size >> 8;
  rx_frame[0x4E] = st.st_size >> 16;
  rx_frame[0x4F] = st.st_size >> 24;
  rx_frame[0x50] = cluster & 0xFF;
  rx_frame[0x51] = cluster >> 8;
  if ((action & 0x0F) == 0x01) {
    rx_frame[0x52] = 0x01; /* Open */
  } else if ((action & 0xF0) == 0x10) {
    rx_frame[0x52] = 0x02; /* Create */
  } else if ((action & 0x0F) == 0x02) {
    rx_frame[0x52] = 0x03; /* Truncate */
  }
  rx_frame[0x53] = 0x00;
  rx_frame[0x54] = attrib & 0x7F;

  edfs_set_result(rx_frame, EDFS_RESULT_OK);
  return 0x55;
}



static uint16_t edfs_read(uint8_t rx_frame[], uint32_t offset,
  uint16_t cluster, uint16_t len)
{
  FILE *fh;
  size_t read_len;
  char tmp_path[PATH_MAX];
  char *p;

  p = edfs_cluster_lookup(cluster);
  if (p == NULL) {
    edfs_set_result(rx_frame, EDFS_RESULT_PATH_NOT_FOUND);
    return 0x3C;
  }

  if (len > 1450) { /* Approximately MTU minus header. */
    panic("EtherDFS read length > 1450\n");
    edfs_set_result(rx_frame, EDFS_RESULT_PATH_NOT_FOUND);
    return 0x3C;
  }

  snprintf(tmp_path, PATH_MAX, "%s/%s", edfs_root, p);
  fh = fopen(tmp_path, "rb");
  if (fh == NULL) {
    edfs_set_result(rx_frame, EDFS_RESULT_ACCESS_DENIED);
    return 0x3C;
  }

  fseek(fh, offset, SEEK_SET);
  read_len = fread(&rx_frame[0x3C], sizeof(uint8_t), len, fh);
  fclose(fh);

  edfs_set_result(rx_frame, EDFS_RESULT_OK);
  return 0x3C + read_len;
}



static uint16_t edfs_write(uint8_t rx_frame[], uint32_t offset,
  uint16_t cluster, uint8_t data[], uint16_t len)
{
  FILE *fh;
  struct stat st;
  size_t write_len;
  char tmp_path[PATH_MAX];
  char *p;

  p = edfs_cluster_lookup(cluster);
  if (p == NULL) {
    edfs_set_result(rx_frame, EDFS_RESULT_PATH_NOT_FOUND);
    return 0x3C;
  }

  snprintf(tmp_path, PATH_MAX, "%s/%s", edfs_root, p);
  if (stat(tmp_path, &st) != 0) {
    edfs_set_result(rx_frame, EDFS_RESULT_PATH_NOT_FOUND);
    return 0x3C;
  }

  if (st.st_size != offset) {
    panic("EtherDFS write to the middle of a file not supported!\n");
    edfs_set_result(rx_frame, EDFS_RESULT_ACCESS_DENIED);
    return 0x3C;
  }

  fh = fopen(tmp_path, "ab"); /* Append */
  if (fh == NULL) {
    edfs_set_result(rx_frame, EDFS_RESULT_ACCESS_DENIED);
    return 0x3C;
  }

  write_len = fwrite(data, sizeof(uint8_t), len, fh);
  fclose(fh);

  rx_frame[0x3C] = write_len & 0xFF;
  rx_frame[0x3D] = write_len >> 8;

  edfs_set_result(rx_frame, EDFS_RESULT_OK);
  return 0x3E;
}



static uint16_t edfs_rmdir(uint8_t rx_frame[], char *path83)
{
  char tmp_path[PATH_MAX];
  char *p;
  uint16_t cluster;

  p = edfs_cluster_lookup_83(path83, &cluster);
  if (p == NULL) {
    edfs_set_result(rx_frame, EDFS_RESULT_PATH_NOT_FOUND);
    return 0x3C;
  }

  snprintf(tmp_path, PATH_MAX, "%s/%s", edfs_root, p);
  if (rmdir(tmp_path) == 0) {
    edfs_cluster_unregister(p);
    edfs_set_result(rx_frame, EDFS_RESULT_OK);
  } else {
    edfs_set_result(rx_frame, EDFS_RESULT_ACCESS_DENIED);
  }

  return 0x3C;
}



static uint16_t edfs_mkdir(uint8_t rx_frame[], char *path83)
{
  char tmp_path[PATH_MAX];
  char path[EDFS_PATH_MAX];

  path83_to_unixpath(path83, path);

  snprintf(tmp_path, PATH_MAX, "%s/%s", edfs_root, path);
  if (mkdir(tmp_path, 0777) == 0) {
    edfs_cluster_register(path, path83);
    edfs_set_result(rx_frame, EDFS_RESULT_OK);
  } else {
    edfs_set_result(rx_frame, EDFS_RESULT_ACCESS_DENIED);
  }

  return 0x3C;
}



static uint16_t edfs_chdir(uint8_t rx_frame[], char *path83)
{
  struct stat st;
  char tmp_path[PATH_MAX];
  char *p;
  uint16_t cluster;

  p = edfs_cluster_lookup_83(path83, &cluster);
  if (p == NULL) {
    edfs_set_result(rx_frame, EDFS_RESULT_PATH_NOT_FOUND);
    return 0x3C;
  }

  snprintf(tmp_path, PATH_MAX, "%s/%s", edfs_root, p);
  if (stat(tmp_path, &st) != 0) {
    edfs_set_result(rx_frame, EDFS_RESULT_PATH_NOT_FOUND);
    return 0x3C;
  }

  if (S_ISDIR(st.st_mode)) {
    edfs_set_result(rx_frame, EDFS_RESULT_OK);
  } else {
    edfs_set_result(rx_frame, EDFS_RESULT_PATH_NOT_FOUND);
  }

  return 0x3C;
}



static uint16_t edfs_rename(uint8_t rx_frame[],
  char *path83_src, char *path83_dst)
{
  char tmp_path_src[PATH_MAX];
  char tmp_path_dst[PATH_MAX];
  char path[EDFS_PATH_MAX];
  char *p;
  uint16_t cluster;

  p = edfs_cluster_lookup_83(path83_src, &cluster);
  if (p == NULL) {
    edfs_set_result(rx_frame, EDFS_RESULT_FILE_NOT_FOUND);
    return 0x3C;
  }

  path83_to_unixpath(path83_dst, path);

  snprintf(tmp_path_src, PATH_MAX, "%s/%s", edfs_root, p);
  snprintf(tmp_path_dst, PATH_MAX, "%s/%s", edfs_root, path);
  if (rename(tmp_path_src, tmp_path_dst) == 0) {
    edfs_cluster_unregister(p);
    edfs_cluster_register(path, path83_dst);
    edfs_set_result(rx_frame, EDFS_RESULT_OK);
  } else {
    edfs_set_result(rx_frame, EDFS_RESULT_ACCESS_DENIED);
  }

  return 0x3C;
}



static uint16_t edfs_delete(uint8_t rx_frame[], char *path83)
{
  char tmp_path[PATH_MAX];
  char *p;
  uint16_t cluster;

  /* NOTE: Patterns like "????????.???" to delete all files not supported! */

  p = edfs_cluster_lookup_83(path83, &cluster);
  if (p == NULL) {
    edfs_set_result(rx_frame, EDFS_RESULT_FILE_NOT_FOUND);
    return 0x3C;
  }

  snprintf(tmp_path, PATH_MAX, "%s/%s", edfs_root, p);
  if (unlink(tmp_path) == 0) {
    edfs_cluster_unregister(p);
    edfs_set_result(rx_frame, EDFS_RESULT_OK);
  } else {
    edfs_set_result(rx_frame, EDFS_RESULT_ACCESS_DENIED);
  }

  return 0x3C;
}



static uint16_t edfs_getattr(uint8_t rx_frame[], char *path83)
{
  struct stat st;
  char tmp_path[PATH_MAX];
  char *p;
  uint16_t cluster;
  uint16_t dos_date;
  uint16_t dos_time;

  p = edfs_cluster_lookup_83(path83, &cluster);
  if (p == NULL) {
    edfs_set_result(rx_frame, EDFS_RESULT_PATH_NOT_FOUND);
    return 0x3C;
  }

  snprintf(tmp_path, PATH_MAX, "%s/%s", edfs_root, p);
  if (stat(tmp_path, &st) != 0) {
    edfs_set_result(rx_frame, EDFS_RESULT_PATH_NOT_FOUND);
    return 0x3C;
  }

  if (S_ISDIR(st.st_mode)) {
    rx_frame[0x44] = 0x10; /* Directory */
  } else {
    rx_frame[0x44] = 0x00; /* File */
  }

  dos_time = time_to_dos_time(st.st_mtime);
  dos_date = time_to_dos_date(st.st_mtime);

  rx_frame[0x3C] = dos_time & 0xFF;
  rx_frame[0x3D] = dos_time >> 8;
  rx_frame[0x3E] = dos_date & 0xFF;
  rx_frame[0x3F] = dos_date >> 8;
  rx_frame[0x40] = st.st_size & 0xFF;
  rx_frame[0x41] = st.st_size >> 8;
  rx_frame[0x42] = st.st_size >> 16;
  rx_frame[0x43] = st.st_size >> 24;

  edfs_set_result(rx_frame, EDFS_RESULT_OK);
  return 0x45;
}



void edfs_handle_packet(net_t *net, uint8_t tx_frame[], uint16_t tx_len)
{
  uint8_t func;
  uint8_t ver;
  uint16_t action;
  uint16_t attrib;
  uint16_t checksum;
  uint16_t cluster;
  uint16_t len;
  uint16_t mode;
  uint16_t pos;
  uint32_t offset;
  char path[PATH_MAX];
  char path_dst[PATH_MAX];

  if (edfs_inited == false) {
    return;
  }

  ver   = tx_frame[0x38] & 0x7F;
  func  = tx_frame[0x3B];

  if (ver != 2) {
    panic("Unsupported EtherDFS version: %d\n", ver);
    return;
  }

  switch (func) {
  case EDFS_RMDIR:
    edfs_trace("RMDIR, s='%s'\n", edfs_path(tx_frame, tx_len, 0x3C, path));
    net->rx_len = edfs_rmdir(net->rx_frame,
      edfs_path(tx_frame, tx_len, 0x3C, path));
    break;

  case EDFS_MKDIR:
    edfs_trace("MKDIR, s='%s'\n", edfs_path(tx_frame, tx_len, 0x3C, path));
    net->rx_len = edfs_mkdir(net->rx_frame,
      edfs_path(tx_frame, tx_len, 0x3C, path));
    break;

  case EDFS_CHDIR:
    edfs_trace("CHDIR, s='%s'\n", edfs_path(tx_frame, tx_len, 0x3C, path));
    net->rx_len = edfs_chdir(net->rx_frame,
      edfs_path(tx_frame, tx_len, 0x3C, path));
    break;

  case EDFS_CLOSEFILE:
    cluster  = tx_frame[0x3C];
    cluster += tx_frame[0x3D] << 8;
    edfs_trace("CLOSEFILE, S=0x%04x\n", cluster);
    edfs_set_result(net->rx_frame, EDFS_RESULT_OK);
    net->rx_len = 0x3C;
    break;

  case EDFS_READFILE:
    offset   = tx_frame[0x3C];
    offset  += tx_frame[0x3D] << 8;
    offset  += tx_frame[0x3E] << 16;
    offset  += tx_frame[0x3F] << 24;
    cluster  = tx_frame[0x40];
    cluster += tx_frame[0x41] << 8;
    len      = tx_frame[0x42];
    len     += tx_frame[0x43] << 8;
    edfs_trace("READFILE, O=0x%08x, S=0x%04x, L=0x%04x\n",
      offset, cluster, len);
    net->rx_len = edfs_read(net->rx_frame, offset, cluster, len);
    break;

  case EDFS_WRITEFILE:
    offset   = tx_frame[0x3C];
    offset  += tx_frame[0x3D] << 8;
    offset  += tx_frame[0x3E] << 16;
    offset  += tx_frame[0x3F] << 24;
    cluster  = tx_frame[0x40];
    cluster += tx_frame[0x41] << 8;
    edfs_trace("WRITEFILE, O=0x%08x, S=0x%04x L=0x%04x\n", offset, cluster,
      tx_len - 0x42);
    net->rx_len = edfs_write(net->rx_frame, offset, cluster,
      &tx_frame[0x42], tx_len - 0x42);
    break;

  case EDFS_DISKSPACE:
    edfs_trace("DISKSPACE\n");
    /* Just report 2GB of free space. */
    net->rx_frame[0x3A] = 0x01;
    net->rx_frame[0x3B] = 0x00;
    net->rx_frame[0x3C] = 0xFF;
    net->rx_frame[0x3D] = 0xFF;
    net->rx_frame[0x3E] = 0x00;
    net->rx_frame[0x3F] = 0x80;
    net->rx_frame[0x40] = 0xFF;
    net->rx_frame[0x41] = 0xFF;
    net->rx_len = 0x42;
    break;

  case EDFS_SETATTR:
    edfs_trace("SETATTR, A=0x%02x, f='%s'\n", tx_frame[0x3C],
      edfs_path(tx_frame, tx_len, 0x3D, path));
    /* Do nothing, just report OK. */
    edfs_set_result(net->rx_frame, EDFS_RESULT_OK);
    net->rx_len = 0x3C;
    break;

  case EDFS_GETATTR:
    edfs_trace("GETATTR, f='%s'\n", edfs_path(tx_frame, tx_len, 0x3C, path));
    net->rx_len = edfs_getattr(net->rx_frame,
      edfs_path(tx_frame, tx_len, 0x3C, path));
    break;

  case EDFS_FINDFIRST:
    edfs_trace("FINDFIRST, A=0x%02x, f='%s'\n", tx_frame[0x3C],
      edfs_path(tx_frame, tx_len, 0x3D, path));
    net->rx_len = edfs_find(net->rx_frame, tx_frame[0x3C],
      edfs_path(tx_frame, tx_len, 0x3D, path), 0, 0);
    break;

  case EDFS_FINDNEXT:
    cluster  = tx_frame[0x3C];
    cluster += tx_frame[0x3D] << 8;
    pos      = tx_frame[0x3E];
    pos     += tx_frame[0x3F] << 8;
    edfs_trace("FINDNEXT, C=0x%04x, p=0x%04x, A=0x%02x, f='%s'\n", cluster,
      pos, tx_frame[0x40], edfs_path(tx_frame, tx_len, 0x41, path));
    net->rx_len = edfs_find(net->rx_frame, tx_frame[0x40],
      edfs_path(tx_frame, tx_len, 0x41, path), cluster, pos);
    break;

  case EDFS_RENAME:
    edfs_trace("RENAME, L=%d, S='%s', D='%s'\n", tx_frame[0x3C],
      edfs_path(tx_frame, 61 + tx_frame[0x3C], 0x3D, path),
      edfs_path(tx_frame, tx_len, 0x3D + tx_frame[0x3C], path_dst));
    net->rx_len = edfs_rename(net->rx_frame,
      edfs_path(tx_frame, 61 + tx_frame[0x3C], 0x3D, path),
      edfs_path(tx_frame, tx_len, 0x3D + tx_frame[0x3C], path_dst));
    break;

  case EDFS_DELETE:
    edfs_trace("DELETE, f='%s'\n", edfs_path(tx_frame, tx_len, 0x3C, path));
    net->rx_len = edfs_delete(net->rx_frame,
      edfs_path(tx_frame, tx_len, 0x3C, path));
    break;

  case EDFS_OPEN:
    attrib  = tx_frame[0x3C];
    attrib += tx_frame[0x3D] << 8;
    edfs_trace("OPEN, f='%s', S=0x%04x\n",
      edfs_path(tx_frame, tx_len, 0x42, path), attrib);
    net->rx_len = edfs_open(net->rx_frame,
      edfs_path(tx_frame, tx_len, 0x42, path), attrib, 0x0001);
    break;

  case EDFS_CREATE:
    attrib  = tx_frame[0x3C];
    attrib += tx_frame[0x3D] << 8;
    edfs_trace("CREATE, f='%s', S=0x%04x\n",
      edfs_path(tx_frame, tx_len, 0x42, path), attrib);
    net->rx_len = edfs_open(net->rx_frame,
      edfs_path(tx_frame, tx_len, 0x42, path), 0x0002, 0x0012);
    break;

  case EDFS_SPOPNFIL:
    attrib  = tx_frame[0x3C];
    attrib += tx_frame[0x3D] << 8;
    action  = tx_frame[0x3E];
    action += tx_frame[0x3F] << 8;
    mode    = tx_frame[0x40];
    mode   += tx_frame[0x41] << 8;
    edfs_trace("SPOPNFIL, f='%s', S=0x%04x, C=0x%04x, M=0x%04x\n",
      edfs_path(tx_frame, tx_len, 0x42, path), attrib, action, mode);
    net->rx_len = edfs_open(net->rx_frame,
      edfs_path(tx_frame, tx_len, 0x42, path), mode, action);
    break;

  default:
    panic("Unhandled EtherDFS function: 0x%02x\n", func);
    edfs_set_result(net->rx_frame, EDFS_RESULT_INVALID_FUNCTION);
    net->rx_len = 0x3C;
    break;
  }

  /* Reply frame: */
  net->rx_frame[0x00] = tx_frame[0x06];     /* / Destination */
  net->rx_frame[0x01] = tx_frame[0x07];     /* | Address     */
  net->rx_frame[0x02] = tx_frame[0x08];     /* |             */
  net->rx_frame[0x03] = tx_frame[0x09];     /* |             */
  net->rx_frame[0x04] = tx_frame[0x0A];     /* |             */
  net->rx_frame[0x05] = tx_frame[0x0B];     /* \             */
  net->rx_frame[0x06] = tx_frame[0x00];     /* / Source      */
  net->rx_frame[0x07] = tx_frame[0x01];     /* | Address     */
  net->rx_frame[0x08] = tx_frame[0x02];     /* |             */
  net->rx_frame[0x09] = tx_frame[0x03];     /* |             */
  net->rx_frame[0x0A] = tx_frame[0x04];     /* |             */
  net->rx_frame[0x0B] = tx_frame[0x05];     /* \             */
  net->rx_frame[0x0C] = tx_frame[0x0C];     /* / Ethertype   */
  net->rx_frame[0x0D] = tx_frame[0x0D];     /* \             */
  net->rx_frame[0x34] = net->rx_len & 0xFF; /* / Size        */
  net->rx_frame[0x35] = net->rx_len >> 8;   /* \             */
  net->rx_frame[0x38] = tx_frame[0x38];     /* - Version     */
  net->rx_frame[0x39] = tx_frame[0x39];     /* - Sequence    */
  if (tx_frame[0x38] >> 7) {
    checksum = bsd_checksum(&net->rx_frame[0x38], net->rx_len - 0x38);
  } else {
    checksum = 0;
  }
  net->rx_frame[0x36] = checksum & 0xFF;    /* / Checksum    */
  net->rx_frame[0x37] = checksum >> 8;      /* \             */

  net->rx_ready = true;
}



void edfs_trace_dump(FILE *fh)
{
  int i;

  for (i = edfs_trace_buffer_n; i < EDFS_TRACE_BUFFER_SIZE; i++) {
    if (edfs_trace_buffer[i][0] != '\0') {
      fprintf(fh, edfs_trace_buffer[i]);
    }
  }
  for (i = 0; i < edfs_trace_buffer_n; i++) {
    if (edfs_trace_buffer[i][0] != '\0') {
      fprintf(fh, edfs_trace_buffer[i]);
    }
  }
}



