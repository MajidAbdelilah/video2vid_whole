#include "serverd.h"

bool change_video_name(char*name) {
  if (access(name, F_OK) == 0) {
    // file exists
    // change name
    unsigned int video_name_len = strlen(name);
    for (unsigned int i = video_name_len; i >= 0; i--) {
      name[i + 1] =
          name[i];
      if (name[i + 1] == '.') {
        name[i] = '2';
        return change_video_name(name);
      }
    }

  } else {
    return true;
  }
  return false;
}
