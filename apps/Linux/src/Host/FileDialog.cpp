#include "Host/FileDialog.h"
#include "Host/WindowSystem.h"
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <thread>
#ifdef MINICAD_LINUX_DBUS
#include <dbus/dbus.h>
#endif

extern char** environ;

namespace MiniCAD
{
    namespace
    {
        std::string GlobPattern(const std::string& ext)
        {
            if (ext == "*")
                return "*";
            std::string p = "*.";
            for (char c : ext)
            {
                const unsigned char u = static_cast<unsigned char>(c);
                if (std::isalpha(u))
                {
                    p += '[';
                    p += static_cast<char>(std::tolower(u));
                    p += static_cast<char>(std::toupper(u));
                    p += ']';
                }
                else
                    p += c;
            }
            return p;
        }

        // 返回 false：程序没能启动
        bool RunCapture(const std::vector<std::string>& args, std::string& out, int& exitCode, std::string& error)
        {
            int fds[2];
            if (pipe2(fds, O_CLOEXEC) != 0)
            {
                error = std::strerror(errno);
                return false;
            }
            posix_spawn_file_actions_t actions;
            posix_spawn_file_actions_init(&actions);
            posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
            posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
            std::vector<char*> argv;
            for (const std::string& a : args)
                argv.push_back(const_cast<char*>(a.c_str()));
            argv.push_back(nullptr);
            pid_t pid = 0;
            const int rc = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
            posix_spawn_file_actions_destroy(&actions);
            close(fds[1]);
            if (rc != 0)
            {
                close(fds[0]);
                error = args[0] + ": " + std::strerror(rc);
                return false;
            }
            char buf[4096];
            for (;;)
            {
                const ssize_t n = read(fds[0], buf, sizeof(buf));
                if (n < 0 && errno == EINTR)
                    continue;
                if (n <= 0)
                    break;
                out.append(buf, static_cast<size_t>(n));
            }
            close(fds[0]);
            int status = 0;
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            if (exitCode == 127)
            {
                error = args[0] + ": " + std::strerror(ENOENT);
                return false;
            }
            return true;
        }

        FileDialogResult RunZenity(const FileDialogRequest& r)
        {
            using Mode = FileDialogRequest::Mode;
            std::vector<std::string> args = { "zenity", "--file-selection", "--title=" + r.title };
            if (r.mode == Mode::OpenMultiple)
            {
                args.push_back("--multiple");
                args.push_back("--separator=\n");
            }
            if (r.mode == Mode::Folder)
                args.push_back("--directory");
            if (r.mode == Mode::Save)
            {
                args.push_back("--save");
                if (!r.currentName.empty())
                    args.push_back("--filename=" + r.currentName);
            }
            if (r.mode != Mode::Folder)
                for (const FileDialogFilter& f : r.filters)
                {
                    std::string arg = "--file-filter=" + f.name + " |";
                    for (const std::string& ext : f.extensions)
                        arg += " " + GlobPattern(ext);
                    args.push_back(std::move(arg));
                }

            FileDialogResult result;
            std::string out;
            int code = 0;
            if (!RunCapture(args, out, code, result.error))
                return result;
            if (code != 0)
                return result;
            size_t pos = 0;
            while (pos < out.size())
            {
                size_t end = out.find('\n', pos);
                if (end == std::string::npos)
                    end = out.size();
                if (end > pos)
                    result.paths.push_back(out.substr(pos, end - pos));
                pos = end + 1;
            }
            return result;
        }

#ifdef MINICAD_LINUX_DBUS
        void AppendString(DBusMessageIter* it, const char* s)
        {
            dbus_message_iter_append_basic(it, DBUS_TYPE_STRING, &s);
        }

        template <class F>
        void AppendEntry(DBusMessageIter* dict, const char* key, const char* signature, F&& value)
        {
            DBusMessageIter entry, variant;
            dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
            AppendString(&entry, key);
            dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, signature, &variant);
            value(&variant);
            dbus_message_iter_close_container(&entry, &variant);
            dbus_message_iter_close_container(dict, &entry);
        }

        void AppendBool(DBusMessageIter* dict, const char* key, bool value)
        {
            AppendEntry(dict, key, "b", [value](DBusMessageIter* v)
            {
                const dbus_bool_t b = value ? TRUE : FALSE;
                dbus_message_iter_append_basic(v, DBUS_TYPE_BOOLEAN, &b);
            });
        }

        void AppendFilter(DBusMessageIter* parent, const FileDialogFilter& f)
        {
            DBusMessageIter st, patterns;
            dbus_message_iter_open_container(parent, DBUS_TYPE_STRUCT, nullptr, &st);
            AppendString(&st, f.name.c_str());
            dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "(us)", &patterns);
            for (const std::string& ext : f.extensions)
            {
                const std::string glob = GlobPattern(ext);
                DBusMessageIter p;
                const dbus_uint32_t kind = 0;
                dbus_message_iter_open_container(&patterns, DBUS_TYPE_STRUCT, nullptr, &p);
                dbus_message_iter_append_basic(&p, DBUS_TYPE_UINT32, &kind);
                AppendString(&p, glob.c_str());
                dbus_message_iter_close_container(&patterns, &p);
            }
            dbus_message_iter_close_container(&st, &patterns);
            dbus_message_iter_close_container(parent, &st);
        }

        void ParseResponse(DBusMessage* msg, const FileDialogRequest& r, FileDialogResult& result)
        {
            DBusMessageIter it;
            if (!dbus_message_iter_init(msg, &it) || dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_UINT32)
                return;
            dbus_uint32_t response = 2;
            dbus_message_iter_get_basic(&it, &response);
            if (response != 0 || !dbus_message_iter_next(&it) || dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY)
                return;
            DBusMessageIter dict;
            dbus_message_iter_recurse(&it, &dict);
            for (; dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY; dbus_message_iter_next(&dict))
            {
                DBusMessageIter entry, value;
                dbus_message_iter_recurse(&dict, &entry);
                const char* key = nullptr;
                dbus_message_iter_get_basic(&entry, &key);
                dbus_message_iter_next(&entry);
                dbus_message_iter_recurse(&entry, &value);
                if (key && std::strcmp(key, "uris") == 0 && dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_ARRAY)
                {
                    DBusMessageIter uris;
                    dbus_message_iter_recurse(&value, &uris);
                    for (; dbus_message_iter_get_arg_type(&uris) == DBUS_TYPE_STRING; dbus_message_iter_next(&uris))
                    {
                        const char* uri = nullptr;
                        dbus_message_iter_get_basic(&uris, &uri);
                        if (std::string path = FileUriToPath(uri ? uri : ""); !path.empty())
                            result.paths.push_back(std::move(path));
                    }
                }
                else if (key && std::strcmp(key, "current_filter") == 0 && dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_STRUCT)
                {
                    DBusMessageIter st;
                    dbus_message_iter_recurse(&value, &st);
                    const char* name = nullptr;
                    if (dbus_message_iter_get_arg_type(&st) == DBUS_TYPE_STRING)
                        dbus_message_iter_get_basic(&st, &name);
                    for (size_t i = 0; name && i < r.filters.size(); ++i)
                        if (r.filters[i].name == name)
                            result.filter = static_cast<int>(i);
                }
            }
        }

        // available = false：没有 portal（会话总线、服务或接口不存在），改用 zenity
        FileDialogResult RunPortal(const FileDialogRequest& r, bool& available)
        {
            using Mode = FileDialogRequest::Mode;
            static std::atomic<unsigned> counter{ 0 };
            FileDialogResult result;
            available = false;

            DBusError err;
            dbus_error_init(&err);
            DBusConnection* conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
            if (!conn)
            {
                result.error = err.message ? err.message : "D-Bus";
                dbus_error_free(&err);
                return result;
            }
            dbus_connection_set_exit_on_disconnect(conn, FALSE);
            struct Closer
            {
                DBusConnection* c;
                ~Closer() { dbus_connection_close(c); dbus_connection_unref(c); }
            } closer{ conn };

            dbus_bus_add_match(conn, "type='signal',interface='org.freedesktop.portal.Request',member='Response'", &err);
            if (dbus_error_is_set(&err))
            {
                result.error = err.message;
                dbus_error_free(&err);
                return result;
            }

            std::string sender = dbus_bus_get_unique_name(conn);
            if (!sender.empty() && sender[0] == ':')
                sender.erase(0, 1);
            for (char& c : sender)
                if (c == '.')
                    c = '_';
            const std::string token    = "minicad" + std::to_string(++counter);
            const std::string expected = "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;

            DBusMessage* msg = dbus_message_new_method_call("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                                            "org.freedesktop.portal.FileChooser", r.mode == Mode::Save ? "SaveFile" : "OpenFile");
            DBusMessageIter args, dict;
            dbus_message_iter_init_append(msg, &args);
            AppendString(&args, r.parent.c_str());
            AppendString(&args, r.title.c_str());
            dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &dict);
            AppendEntry(&dict, "handle_token", "s", [&](DBusMessageIter* v) { AppendString(v, token.c_str()); });
            AppendBool(&dict, "modal", true);
            if (r.mode == Mode::OpenMultiple)
                AppendBool(&dict, "multiple", true);
            if (r.mode == Mode::Folder)
                AppendBool(&dict, "directory", true);
            if (r.mode == Mode::Save && !r.currentName.empty())
                AppendEntry(&dict, "current_name", "s", [&](DBusMessageIter* v) { AppendString(v, r.currentName.c_str()); });
            if (r.mode != Mode::Folder && !r.filters.empty())
            {
                AppendEntry(&dict, "filters", "a(sa(us))", [&](DBusMessageIter* v)
                {
                    DBusMessageIter list;
                    dbus_message_iter_open_container(v, DBUS_TYPE_ARRAY, "(sa(us))", &list);
                    for (const FileDialogFilter& f : r.filters)
                        AppendFilter(&list, f);
                    dbus_message_iter_close_container(v, &list);
                });
                AppendEntry(&dict, "current_filter", "(sa(us))", [&](DBusMessageIter* v) { AppendFilter(v, r.filters.front()); });
            }
            dbus_message_iter_close_container(&args, &dict);

            DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, DBUS_TIMEOUT_USE_DEFAULT, &err);
            dbus_message_unref(msg);
            if (!reply)
            {
                result.error = err.message ? err.message : "xdg-desktop-portal";
                dbus_error_free(&err);
                return result;
            }
            std::string handle = expected;
            const char* path = nullptr;
            if (dbus_message_get_args(reply, &err, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID) && path)
                handle = path;
            dbus_error_free(&err);
            dbus_message_unref(reply);
            available = true;
            result.error.clear();

            while (dbus_connection_read_write(conn, -1))
                while (DBusMessage* m = dbus_connection_pop_message(conn))
                {
                    const char* p = dbus_message_get_path(m);
                    const bool mine = dbus_message_is_signal(m, "org.freedesktop.portal.Request", "Response") && p &&
                                      (handle == p || expected == p);
                    if (mine)
                        ParseResponse(m, r, result);
                    dbus_message_unref(m);
                    if (mine)
                        return result;
                }
            result.error = "xdg-desktop-portal";
            return result;
        }
#endif
    }

    FileDialogResult RunFileDialog(const FileDialogRequest& request)
    {
        std::string portalError;
#ifdef MINICAD_LINUX_DBUS
        static const bool threads = dbus_threads_init_default();
        (void)threads;
        bool available = false;
        FileDialogResult result = RunPortal(request, available);
        if (available)
            return result;
        portalError = result.error;
#endif
        FileDialogResult result2 = RunZenity(request);
        if (!result2.error.empty() && !portalError.empty())
            result2.error = "xdg-desktop-portal: " + portalError + "\n" + result2.error;
        return result2;
    }

    bool LaunchDetached(const std::vector<std::string>& args)
    {
        if (args.empty())
            return false;
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
        std::vector<char*> argv;
        for (const std::string& a : args)
            argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        pid_t pid = 0;
        const int rc = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        if (rc != 0)
            return false;
        std::thread([pid]
        {
            int status = 0;
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        }).detach();
        return true;
    }
}
