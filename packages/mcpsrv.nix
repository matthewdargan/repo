{
  perSystem = {
    lib,
    pkgs,
    ...
  }: {
    packages = {
      "mcpsrv" = pkgs.clangStdenv.mkDerivation {
        buildInputs = with pkgs; [tree-sitter];
        buildPhase = ''
          mkdir -p treesitter/tree_sitter
          cp ${pkgs.tree-sitter-grammars.tree-sitter-c.src}/src/parser.c treesitter/parser_c.c
          cp ${pkgs.tree-sitter-grammars.tree-sitter-c.src}/src/tree_sitter/parser.h treesitter/tree_sitter/parser.h
          clang -O3 -I. -Itreesitter -g -fdiagnostics-absolute-paths -Wall -Wextra \
             -ltree-sitter cmd/mcpsrv/main.c -o mcpsrv
        '';
        installPhase = ''
          mkdir -p "$out/bin"
          cp mcpsrv "$out/bin"
        '';
        meta = with lib; {
          description = "MCP server for codebase symbol analysis";
          homepage = "https://github.com/matthewdargan/repo";
          license = licenses.bsd3;
          maintainers = with maintainers; [matthewdargan];
        };
        pname = "mcpsrv";
        src = ../.;
        version = "0.1.0";
      };
      "mcpsrv-debug" = pkgs.clangStdenv.mkDerivation {
        buildInputs = with pkgs; [tree-sitter];
        buildPhase = ''
          mkdir -p treesitter/tree_sitter
          cp ${pkgs.tree-sitter-grammars.tree-sitter-c.src}/src/parser.c treesitter/parser_c.c
          cp ${pkgs.tree-sitter-grammars.tree-sitter-c.src}/src/tree_sitter/parser.h treesitter/tree_sitter/parser.h
          clang -O0 -g -fsanitize=address -fno-omit-frame-pointer \
             -I. -Itreesitter -fdiagnostics-absolute-paths -Wall -Wextra \
             -ltree-sitter cmd/mcpsrv/main.c -o mcpsrv
        '';
        dontStrip = true;
        installPhase = ''
          mkdir -p "$out/bin"
          cp mcpsrv "$out/bin"
        '';
        meta = with lib; {
          description = "MCP server for codebase symbol analysis";
          homepage = "https://github.com/matthewdargan/repo";
          license = licenses.bsd3;
          maintainers = with maintainers; [matthewdargan];
        };
        pname = "mcpsrv";
        src = ../.;
        version = "0.1.0";
      };
    };
  };
}
