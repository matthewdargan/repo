{
  perSystem = {
    pkgs,
    self',
    ...
  }: {
    checks = {
      "9pfs-test" = let
        server = self'.packages."9pfs-debug";
        testClient = self'.packages."9pfs-test-debug";
      in
        pkgs.runCommand "9pfs-test-check" {
          buildInputs = [server testClient pkgs.coreutils];
        } ''
          set -e

          testdir=$(mktemp -d)
          trap "rm -rf $testdir" EXIT

          test_port=19999
          test_addr="tcp!localhost!$test_port"

          ${server}/bin/9pfs --root=$testdir $test_addr &
          server_pid=$!
          trap "kill $server_pid 2>/dev/null || true; rm -rf $testdir" EXIT
          sleep 2

          ${testClient}/bin/9pfs-test $test_addr > test_output.txt 2>&1 || true
          cat test_output.txt

          failed_count=$(grep -oP '\d+(?= failed)' test_output.txt || echo "0")
          passed_count=$(grep -oP '\d+(?= passed)' test_output.txt || echo "0")

          if [ "$failed_count" != "0" ]; then
            echo "[ERROR] $failed_count tests failed"
            exit 1
          fi

          if [ "$passed_count" = "0" ]; then
            echo "[ERROR] no tests ran"
            exit 1
          fi

          echo "[$passed_count tests passed]"
          touch $out
        '';

      "9auth-test" = let
        authAgent = self'.packages."9auth-debug";
        server = self'.packages."9pfs-debug";
        testClient = self'.packages."9auth-test-debug";
      in
        pkgs.runCommand "9auth-test-check" {
          buildInputs = [authAgent server testClient pkgs.coreutils];
        } ''
          set -e

          testdir=$(mktemp -d)
          trap "rm -rf $testdir" EXIT

          mkdir -p "$testdir"/{fs-root,run/9auth,var/lib/9auth}

          echo "test file 1" > "$testdir/fs-root/test1.txt"
          echo "test file 2" > "$testdir/fs-root/test2.txt"
          mkdir -p "$testdir/fs-root/testdir"
          echo "test file 3" > "$testdir/fs-root/testdir/test3.txt"

          auth_socket="$testdir/run/9auth/socket"
          auth_keys="$testdir/var/lib/9auth/keys"
          fs_socket="$testdir/run/9pfs"

          ${authAgent}/bin/9auth --socket-path="$auth_socket" --keys-path="$auth_keys" &
          auth_pid=$!
          trap "kill $auth_pid 2>/dev/null || true; rm -rf $testdir" EXIT
          sleep 2

          ${server}/bin/9pfs --root="$testdir/fs-root" --auth-daemon=unix!"$auth_socket" --auth-id=e2etest.local unix!"$fs_socket" &
          fs_pid=$!
          trap "kill $fs_pid $auth_pid 2>/dev/null || true; rm -rf $testdir" EXIT
          sleep 2

          ${testClient}/bin/9auth-test unix!"$auth_socket" unix!"$fs_socket" > test_output.txt 2>&1 || true
          cat test_output.txt

          failed_count=$(grep -oP '\d+(?= failed)' test_output.txt || echo "0")
          passed_count=$(grep -oP '\d+(?= passed)' test_output.txt || echo "0")

          kill $fs_pid $auth_pid 2>/dev/null || true
          wait $fs_pid $auth_pid 2>/dev/null || true

          if [ "$failed_count" != "0" ]; then
            echo "[ERROR] $failed_count tests failed"
            exit 1
          fi

          if [ "$passed_count" = "0" ]; then
            echo "[ERROR] no tests ran"
            exit 1
          fi

          echo "[$passed_count tests passed]"
          touch $out
        '';
    };
  };
}
