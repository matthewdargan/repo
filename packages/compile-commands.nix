{
  perSystem = {pkgs, ...}: {
    packages.compile-commands = pkgs.writeShellApplication {
      name = "generate-compile-commands";
      text = ''
        REPO_ROOT="''${1:-.}"
        cat > "$REPO_ROOT/compile_commands.json" << EOF
        [
          {
            "directory": "$REPO_ROOT",
            "command": "clang -I. -D_GNU_SOURCE -g -Wall -Wextra -Wno-unknown-warning-option -Wno-unused-function -Wno-unused-variable -Wno-unused-value -O0 -DBUILD_DEBUG=1 -c base/inc.c",
            "file": "base/inc.c"
          },
          {
            "directory": "$REPO_ROOT",
            "command": "clang -I. -D_GNU_SOURCE -g -Wall -Wextra -Wno-unknown-warning-option -Wno-unused-function -Wno-unused-variable -Wno-unused-value -O0 -DBUILD_DEBUG=1 -c 9p/inc.c",
            "file": "9p/inc.c"
          },
          {
            "directory": "$REPO_ROOT",
            "command": "clang -I. -D_GNU_SOURCE -g -Wall -Wextra -Wno-unknown-warning-option -Wno-unused-function -Wno-unused-variable -Wno-unused-value -O0 -DBUILD_DEBUG=1 -c http/inc.c",
            "file": "http/inc.c"
          },
          {
            "directory": "$REPO_ROOT",
            "command": "clang -I. -D_GNU_SOURCE -g -Wall -Wextra -Wno-unknown-warning-option -Wno-unused-function -Wno-unused-variable -Wno-unused-value -O0 -DBUILD_DEBUG=1 -c json/inc.c",
            "file": "json/inc.c"
          },
          {
            "directory": "$REPO_ROOT",
            "command": "clang -I. -D_GNU_SOURCE -g -Wall -Wextra -Wno-unknown-warning-option -Wno-unused-function -Wno-unused-variable -Wno-unused-value -O0 -DBUILD_DEBUG=1 cmd/9bind/main.c -o 9bind",
            "file": "cmd/9bind/main.c"
          },
          {
            "directory": "$REPO_ROOT",
            "command": "clang -I. -D_GNU_SOURCE -g -Wall -Wextra -Wno-unknown-warning-option -Wno-unused-function -Wno-unused-variable -Wno-unused-value -O0 -DBUILD_DEBUG=1 cmd/9mount/main.c -o 9mount",
            "file": "cmd/9mount/main.c"
          },
          {
            "directory": "$REPO_ROOT",
            "command": "clang -I. -D_GNU_SOURCE -g -Wall -Wextra -Wno-unknown-warning-option -Wno-unused-function -Wno-unused-variable -Wno-unused-value -O0 -DBUILD_DEBUG=1 cmd/9pfs/main.c -o 9pfs",
            "file": "cmd/9pfs/main.c"
          },
          {
            "directory": "$REPO_ROOT",
            "command": "clang -I. -D_GNU_SOURCE -g -Wall -Wextra -Wno-unknown-warning-option -Wno-unused-function -Wno-unused-variable -Wno-unused-value -O0 -DBUILD_DEBUG=1 cmd/9pfs-test/main.c -o 9pfs-test",
            "file": "cmd/9pfs-test/main.c"
          },
          {
            "directory": "$REPO_ROOT",
            "command": "clang -I. -D_GNU_SOURCE -g -Wall -Wextra -Wno-unknown-warning-option -Wno-unused-function -Wno-unused-variable -Wno-unused-value -O0 -DBUILD_DEBUG=1 cmd/9p/main.c -o 9p",
            "file": "cmd/9p/main.c"
          },
          {
            "directory": "$REPO_ROOT",
            "command": "clang -I. -D_GNU_SOURCE -g -Wall -Wextra -Wno-unknown-warning-option -Wno-unused-function -Wno-unused-variable -Wno-unused-value -O0 -DBUILD_DEBUG=1 cmd/9umount/main.c -o 9umount",
            "file": "cmd/9umount/main.c"
          },
          {
            "directory": "$REPO_ROOT",
            "command": "clang -I. -D_GNU_SOURCE -g -Wall -Wextra -Wno-unknown-warning-option -Wno-unused-function -Wno-unused-variable -Wno-unused-value -O0 -DBUILD_DEBUG=1 cmd/authd/main.c -o authd -lm",
            "file": "cmd/authd/main.c"
          },
          {
            "directory": "$REPO_ROOT",
            "command": "clang -I. -D_GNU_SOURCE -g -Wall -Wextra -Wno-unknown-warning-option -Wno-unused-function -Wno-unused-variable -Wno-unused-value -O0 -DBUILD_DEBUG=1 cmd/mount-9p/main.c -o mount-9p",
            "file": "cmd/mount-9p/main.c"
          }
        ]
        EOF
        echo "Generated compile_commands.json in $REPO_ROOT"
      '';
    };
  };
}
