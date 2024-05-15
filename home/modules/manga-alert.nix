{config, ...}: {
  manga-alert = {
    enable = true;
    manga = ["one piece"];
    timer.enable = true;
    user = "${config.home.username}";
  };
}
