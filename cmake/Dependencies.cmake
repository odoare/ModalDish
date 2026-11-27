# ──────────────────────────────────────────────────────────────────────────
# Dependencies.cmake — ModalDish's pinned dependencies.
#
# Included after project() by the root CMakeLists.txt. Resolution (sibling
# clone or pinned commit) is FxmeDeps.cmake's: locally the sibling clones
# (../JUCE, ../FxmeTools, ../clap-juce-extensions) win; release builds
# configure with -DFXME_USE_SIBLINGS=OFF and use the pins below.
# ──────────────────────────────────────────────────────────────────────────

include_guard(GLOBAL)
include(${CMAKE_CURRENT_LIST_DIR}/FxmeDeps.cmake)

fxme_dependency(JUCE https://github.com/juce-framework/JUCE 9.0.4 SHALLOW)
# FxmeTools brings the FEM code (core/FxmeTools/acoustics) and the FxmeNet
# bridge (FxmeTools/net/NetBridge.h).
fxme_dependency(FxmeTools https://github.com/odoare/FxmeTools
    bd9d225ba2275b9d931136af04abd9b73fcbb7b0)
# CLAP format (fxmetools_add_clap). Its last tag predates JUCE 9 support:
# pinned to a commit. Brings the clap and clap-helpers submodules along.
fxme_dependency(ClapJuceExtensions https://github.com/free-audio/clap-juce-extensions
    7adee3a1bd4684d4caa5601100e364abccff4b4d
    SIBLING clap-juce-extensions)

add_subdirectory(${JUCE_DIR} JUCE)

# Registers the `FxmeTools` JUCE module, FxmeCore (the JUCE-free half, where
# the FEM mesh and plate solver live), fxmetools_attach() and
# fxmetools_add_clap().
include(${FXMETOOLS_DIR}/cmake/FxmeTools.cmake)
