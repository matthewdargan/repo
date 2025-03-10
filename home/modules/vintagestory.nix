{pkgs, ...}: {
  home.packages = [
    (pkgs.vintagestory.override {
      dotnet-runtime_7 = pkgs.dotnet-runtime_7.overrideAttrs (o: {
        src = o.src.overrideAttrs (o: {
          meta =
            o.meta
            // {
              knownVulnerabilities = [];
            };
        });
        meta =
          o.meta
          // {
            knownVulnerabilities = [];
          };
      });
    })
  ];
}
