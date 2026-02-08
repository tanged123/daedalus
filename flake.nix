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

        # ImGui Bundle — custom derivation (not in nixpkgs)
        imgui-bundle = stdenv.mkDerivation {
          pname = "imgui-bundle";
          version = "1.92.5";
          src = pkgs.fetchgit {
            url = "https://github.com/pthom/imgui_bundle.git";
            rev = "v1.92.5";
            hash = "sha256-1B+YZSX//qso/4ratC8PTyg13Pfg6sPn40j5SlbJWeg=";
            fetchSubmodules = true;
          };
          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
          ];
          buildInputs = with pkgs; [
            glfw
            libGL
            xorg.libX11
            xorg.libXrandr
            xorg.libXinerama
            xorg.libXcursor
            xorg.libXi
            xorg.libXext
          ];
          cmakeFlags = [
            "-DHELLOIMGUI_USE_GLFW3=ON"
            "-DHELLOIMGUI_HAS_OPENGL3=ON"
            "-DIMGUI_BUNDLE_INSTALL_CPP=ON"
            "-DIMGUI_BUNDLE_BUILD_PYTHON=OFF"
            "-DIMGUI_BUNDLE_BUILD_DEMOS=OFF"
            "-DHELLOIMGUI_USE_FREETYPE=OFF"
            "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"
          ];
          postInstall = ''
            # --- Replace broken upstream cmake configs with a clean one ---
            #
            # imgui_bundle's cmake install is deeply broken for external consumers:
            # circular target references, missing find_dependency packages, source-tree
            # path references in helper scripts, wrong _IMPORT_PREFIX computation.
            #
            # Instead of patching, we replace with a minimal config that correctly
            # exports an INTERFACE target pointing at the installed headers and libs.

            rm -rf $out/lib/cmake
            mkdir -p $out/lib/cmake/imgui_bundle

            # Static libraries have circular dependencies (immapp ↔ imgui_md,
            # hello_imgui ↔ imgui_tex_inspect). Use --start-group/--end-group
            # to let the linker resolve cycles.
            LIBS="-Wl,--start-group"
            for lib in $out/lib/*.a; do
              LIBS="$LIBS;$lib"
            done
            LIBS="$LIBS;-Wl,--end-group"

            cat > $out/lib/cmake/imgui_bundle/imgui_bundleConfig.cmake << CMAKECFG
            if(TARGET imgui_bundle::imgui_bundle)
              return()
            endif()
            add_library(imgui_bundle::imgui_bundle INTERFACE IMPORTED)
            set_target_properties(imgui_bundle::imgui_bundle PROPERTIES
              INTERFACE_INCLUDE_DIRECTORIES "$out/include"
              INTERFACE_LINK_LIBRARIES "$LIBS;glfw;GL;stdc++;dl;X11"
              INTERFACE_COMPILE_DEFINITIONS "HELLOIMGUI_USE_GLFW3;HELLOIMGUI_HAS_OPENGL;HELLOIMGUI_HAS_OPENGL3;IMGUI_BUNDLE_WITH_IMPLOT;IMGUI_BUNDLE_WITH_IMPLOT3D;IMGUI_BUNDLE_WITH_IMGUI_NODE_EDITOR"
            )
            CMAKECFG
          '';
          # Remove dangling symlink left by the install
          postFixup = ''
            find $out -xtype l -delete
          '';
        };

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
            imgui-bundle
            # Windowing / OpenGL (needed at link time)
            pkgs.glfw
            pkgs.libGL
            pkgs.freetype
            pkgs.xorg.libX11
            pkgs.xorg.libXrandr
            pkgs.xorg.libXinerama
            pkgs.xorg.libXcursor
            pkgs.xorg.libXi
            pkgs.xorg.libXext
            # Networking (IXWebSocket FetchContent deps)
            pkgs.openssl
            pkgs.zlib
            # Data
            pkgs.nlohmann_json
          ];

          cmakeFlags = [
            "-DBUILD_TESTING=OFF"
            "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"
          ];
        };
      in
      {
        packages = {
          default = daedalusPackage;
          daedalus = daedalusPackage;
          inherit imgui-bundle;
        };

        devShells.default = pkgs.mkShell.override { inherit stdenv; } {
          packages = [
            imgui-bundle
          ]
          ++ (with pkgs; [
            # Build tools
            cmake
            ninja
            pkg-config
            ccache
            # Windowing / OpenGL
            glfw
            libGL
            freetype
            xorg.libX11
            xorg.libXrandr
            xorg.libXinerama
            xorg.libXcursor
            xorg.libXi
            xorg.libXext
            # Networking (IXWebSocket FetchContent deps)
            openssl
            zlib
            # Data formats
            nlohmann_json
            # Testing
            gtest
            # Dev tools
            clang-tools
            llvmPackages_latest.llvm # llvm-cov for coverage
            doxygen
            graphviz
            lcov
            treefmtEval.config.build.wrapper
          ]);

          shellHook = ''
            # Propagate Nix cmake paths so find_package() works in manual builds
            export CMAKE_PREFIX_PATH="$NIXPKGS_CMAKE_PREFIX_PATH''${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
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
