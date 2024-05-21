{pkgs, ...}: {
  extraConfigLua = ''
    require("CopilotChat").setup {
      debug = true,
    }
  '';
  extraPlugins = [
    (pkgs.vimUtils.buildVimPlugin {
      name = "copilot-chat-nvim";
      src = pkgs.fetchFromGitHub {
        owner = "copilotc-nvim";
        repo = "copilotchat.nvim";
        rev = "f694cca";
        sha256 = "sha256-jZb+dqGaZEs1h2CbvsxbINfHauwHka9t+jmSJQ/mMFM=";
      };
    })
  ];
  keymaps = [
    {
      action = ":vert Copilot panel<CR>";
      key = "<leader>p";
      mode = ["n"];
    }
  ];
  plugins.copilot-vim.enable = true;
}
