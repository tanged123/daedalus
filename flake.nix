{
  description = "Daedalus: Mission Control Visualization Suite";

  nixConfig = {
    extra-substituters = [ "https://tanged123.cachix.org" ];
    extra-trusted-public-keys = [
      "tanged123.cachix.org-1:S79iH77XKs7/Ap+z9oaafrhmrw6lQ21QDzxyNqg1UVI="
    ];
  };

  inputs = {
    flake-utils.url = "github:numtide/flake-utils";
    treefmt-nix.url = "github:numtide/treefmt-nix";

    # Hermes orchestration platform (provides protocol definitions)
    hermes.url = "github:tanged123/hermes";

    # Use hermes's nixpkgs for consistency across the stack
    nixpkgs.follows = "hermes/nixpkgs";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
      treefmt-nix,
      hermes,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        stdenv = pkgs.llvmPackages_latest.stdenv;

        # Treefmt configuration
        treefmtEval = treefmt-nix.lib.evalModule pkgs {
          projectRootFile = "flake.nix";
          programs.nixfmt.enable = true;
          programs.clang-format.enable = true;
          programs.cmake-format.enable = true;
        };

        # Daedalus package
        daedalusPackage = stdenv.mkDerivation {
          pname = "daedalus";
          version = "0.1.0";
          src = ./.;

          nativeBuildInputs = [
            pkgs.cmake
            pkgs.ninja
            pkgs.pkg-config
          ];

          buildInputs = [
            # ImGui ecosystem
            pkgs.imgui
            # Windowing / OpenGL
            pkgs.glfw
            pkgs.libGL
            # Networking
            pkgs.libwebsockets
            pkgs.openssl
            # Data
            pkgs.nlohmann_json
          ];

          cmakeFlags = [
            "-DBUILD_TESTING=OFF"
          ];
        };
      in
      {
        packages = {
          default = daedalusPackage;
          daedalus = daedalusPackage;
        };

        devShells.default = pkgs.mkShell.override { inherit stdenv; } {
          packages = with pkgs; [
            # Build tools
            cmake
            ninja
            pkg-config
            ccache
            # ImGui ecosystem
            imgui
            # Windowing / OpenGL
            glfw
            libGL
            # Networking
            libwebsockets
            openssl
            # Data formats
            nlohmann_json
            # Testing
            gtest
            # Dev tools
            clang-tools
            doxygen
            graphviz
            lcov
            treefmtEval.config.build.wrapper
          ];

          shellHook = ''
            echo "Daedalus dev environment loaded"
            echo "  - C++ compiler: $(c++ --version | head -1)"
          '';
        };

        formatter = treefmtEval.config.build.wrapper;

        checks = {
          formatting = treefmtEval.config.build.check self;
        };
      }
    );
}
