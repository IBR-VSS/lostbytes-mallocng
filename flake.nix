{
  description = "Mallocng development flake";
  inputs = {
    flake-utils.url = "github:numtide/flake-utils";
    nixpkgs.url = "nixpkgs/nixos-unstable";
  };

  outputs =
    {
      self,
      flake-utils,
      nixpkgs,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        packages.default =
          with pkgs;
          stdenv.mkDerivation {
            name = "mallocng-lit";
            src = ./.;
            nativeBuildInputs = [
              gnumake
            ];
            installPhase = ''
              mkdir -p $out
              cp libmallocng.so $out
            '';
          };
        devShell =
          with pkgs;
          mkShell {
            packages = [
              clang-tools
              bear
            ];
            inputsFrom = [ self.packages.${system}.default ];
          };
      }
    );
}
