/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>

#include "ui.h"
#include "cutils/properties.h"
#include "install.h"
#include "common.h"
#include "adb_install.h"
extern "C" {
#include "minadbd/adb.h"
}

static RecoveryUI* ui = NULL;

static void
set_usb_driver(bool enabled) {
    int fd = open("/sys/class/android_usb/android0/enable", O_WRONLY);
    if (fd < 0) {
        ui->Print("failed to open driver control: %s\n", strerror(errno));
        return;
    }
    if (write(fd, enabled ? "1" : "0", 1) < 0) {
        ui->Print("failed to set driver control: %s\n", strerror(errno));
    }
    if (close(fd) < 0) {
        ui->Print("failed to close driver control: %s\n", strerror(errno));
    }
}

static void
stop_adbd() {
    property_set("ctl.stop", "adbd");
    set_usb_driver(false);
}


static void
maybe_restart_adbd() {
    char value[PROPERTY_VALUE_MAX+1];
    int len = property_get("ro.debuggable", value, NULL);
    if (len == 1 && value[0] == '1') {
        ui->Print("Restarting adbd...\n");
        set_usb_driver(true);
        property_set("ctl.start", "adbd");
    }
}

int
apply_from_adb(RecoveryUI* ui_, int* wipe_cache, const char* install_file) {
    ui = ui_;

    stop_adbd();
    set_usb_driver(true);

    ui->Print("\n\nNow send the package you want to apply\n"
              "to the device with \"adb sideload <filename>\"...\n");

    pid_t child;
	//对子进程fork返回0给它，子进程随时可调用getpid()来获取自己的pid
    if ((child = fork()) == 0) {
		//执行/sbin/recovery,传递的参数为"recovery", "--adbd",execl()其中后缀"l"代表list也就是参数列表的意思，第一参数path字符指针所指向要执行的文件路径， 接下来的参数代表执行该文件时传递的参数列表：argv[0],argv[1]... 最后一个参数须用空指针NULL作结束
        execl("/sbin/recovery", "recovery", "--adbd", NULL);
        _exit(-1);
    }
    int status;
    // TODO(dougz): there should be a way to cancel waiting for a
    // package (by pushing some button combo on the device).  For now
    // you just have to 'adb sideload' a file that's not a valid
    // package, like "/dev/null".
    waitpid(child, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        ui->Print("status %d\n", WEXITSTATUS(status));
    }

    set_usb_driver(false);
    maybe_restart_adbd();

    struct stat st;
    if (stat(ADB_SIDELOAD_FILENAME, &st) != 0) {
        if (errno == ENOENT) {
            ui->Print("No package received.\n");
        } else {
            ui->Print("Error reading package:\n  %s\n", strerror(errno));
        }
        return INSTALL_ERROR;
    }
    return install_package(ADB_SIDELOAD_FILENAME, wipe_cache, install_file);
}
