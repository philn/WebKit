include(FetchContent)

FetchContent_Declare(
  Corrosion
  GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
  GIT_TAG v0.6.0
)
FetchContent_MakeAvailable(Corrosion)

corrosion_import_crate(MANIFEST_PATH ${THIRDPARTY_DIR}/mdns-service-rs/Cargo.toml
  FLAGS --quiet
)
list(APPEND WebKit_LIBRARIES mdns_service)
list(APPEND WebKit_PRIVATE_INCLUDE_DIRECTORIES "${THIRDPARTY_DIR}/mdns-service-rs/")

list(APPEND WebKit_SOURCES
  NetworkProcess/webrtc/NetworkMDNSRegisterLinux.cpp
)
