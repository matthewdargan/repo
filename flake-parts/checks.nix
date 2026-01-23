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
        testClient = self'.packages."9auth-test-debug";
      in
        pkgs.runCommand "9auth-test-check" {
          buildInputs = [testClient pkgs.coreutils];
        } ''
          set -e

          ${testClient}/bin/9auth-test > test_output.txt 2>&1 || true
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
    };
  };
}
