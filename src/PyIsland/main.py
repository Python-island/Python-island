import os
import platform
import subprocess
import sys


def background_task(cmd_args):
    command = cmd_args[0]
    args = cmd_args[1:]

    if not os.path.isabs(command):
        if os.path.exists(command):
            command = os.path.abspath(command)
        else:
            path_dirs = os.environ.get('PATH', '').split(os.pathsep)
            for dir_path in path_dirs:
                full_path = os.path.join(dir_path, command)
                if os.path.exists(full_path):
                    command = full_path
                    break

    try:
        if platform.system() == 'Windows':
            startupinfo = subprocess.STARTUPINFO()
            startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startupinfo.wShowWindow = subprocess.SW_HIDE

            subprocess.Popen(
                [command] + args,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                stdin=subprocess.DEVNULL,
                startupinfo=startupinfo,
                creationflags=subprocess.CREATE_NO_WINDOW | subprocess.DETACHED_PROCESS,
                close_fds=True
            )
        else:
            subprocess.Popen(
                [command] + args,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                stdin=subprocess.DEVNULL,
                start_new_session=True,
                close_fds=True
            )

        return 0

    except FileNotFoundError:
        print(f"错误: 找不到文件 '{cmd_args[0]}'")
        return 1
    except Exception as e:
        print(f"启动进程时出错: {e}")
        return 1


def main():
    return background_task(["_island_instance"])


if __name__ == "__main__":
    sys.exit(main())