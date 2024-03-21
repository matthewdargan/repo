{
  buildGo122Module,
  fetchFromGitHub,
  lib,
}:
buildGo122Module rec {
  doCheck = false;
  meta = with lib; {
    changelog = "https://github.com/golang/tools/releases/tag/${src.rev}";
    description = "Official language server for the Go language";
    homepage = "https://github.com/golang/tools/tree/master/gopls";
    license = licenses.bsd3;
    mainProgram = "gopls";
    maintainers = with maintainers; [mic92 rski SuperSandro2000 zimbatm];
  };
  modRoot = "gopls";
  pname = "gopls";
  src = fetchFromGitHub {
    hash = "sha256-GJ2zc5OgZXwEq12f0PyvgOOUd7cctUbFvdp095VQb9E=";
    owner = "golang";
    repo = "tools";
    rev = "gopls/v${version}";
  };
  subPackages = ["."]; # Build gopls without tests or doc generator.
  vendorHash = "sha256-Xxik0t1BHQPqzrE3Oh3VhODn+IqIVa+TCNqQSnmbBM0=";
  version = "0.15.1";
}
