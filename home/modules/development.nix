_: {
  programs = {
    delta = {
      enable = true;
      enableGitIntegration = true;
    };
    direnv = {
      enable = true;
      nix-direnv.enable = true;
    };
    git = {
      enable = true;
      settings = {
        init.defaultBranch = "main";
        push.autoSetupRemote = true;
        user = {
          email = "matthewdargan57@gmail.com";
          name = "Matthew Dargan";
        };
      };
    };
  };
}
