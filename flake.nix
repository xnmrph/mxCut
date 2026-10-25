{
  description = "mxCut: a fast FFmpeg front end for trimming videos";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    {
      # For NixOS or home-manager: add this overlay, then pkgs.mxcut.
      overlays.default = final: prev: {
        mxcut = self.packages.${prev.stdenv.hostPlatform.system}.default;
      };
    } //
    flake-utils.lib.eachSystem [ "x86_64-linux" "aarch64-linux" ] (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "mxcut";
          # Keep in step with project(VERSION) in CMakeLists.txt; CI checks this.
          version = "1.0.5";
          src = ./.;

          nativeBuildInputs = with pkgs; [ cmake pkg-config qt6.wrapQtAppsHook ];
          buildInputs = with pkgs; [ qt6.qtbase qt6.qtdeclarative mpv-unwrapped ];

          cmakeFlags = [ "-DBUILD_TESTING=OFF" ];

          # The app looks for ffmpeg on PATH, so put a known copy there.
          qtWrapperArgs = [ "--prefix PATH : ${pkgs.lib.makeBinPath [ pkgs.ffmpeg ]}" ];

          meta = {
            description = "Trim videos singly or in batches with FFmpeg";
            mainProgram = "mxcut";
            homepage = "https://github.com/xnmrph/mxCut";
            platforms = pkgs.lib.platforms.linux;
          };
        };

        apps.default = flake-utils.lib.mkApp { drv = self.packages.${system}.default; };

        devShells.default = pkgs.mkShell {
          inputsFrom = [ self.packages.${system}.default ];
          packages = with pkgs; [ ffmpeg gdb ];
        };
      });
}
