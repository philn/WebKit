kind: manual

depends:
- freedesktop-sdk.bst:bootstrap-import.bst
- freedesktop-sdk.bst:components/curl.bst

environment:
  PATH: /usr/bin:/usr/lib/sdk/rust/bin

config:
  build-commands:
  - |
    make libwhisper.so

  install-commands:
  - |
    install -D -m a+r -t "%{install-root}%{includedir}" ./whisper.h
    install -D -m a+r -t "%{install-root}%{libdir}" ./libwhisper.so
    mkdir -p %{install-root}%{datadir}/whisper
    cp models/ggml-*.bin %{install-root}%{datadir}/whisper/

sources:
- kind: tar
  url: github_com:ggerganov/whisper.cpp/archive/refs/tags/v1.4.2.tar.gz
  ref: 1b988dcc77fca55f188dbc4e472f971a80854c1d44309cf3eaab9d5677f175e1
- kind: remote
  url: https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin
  ref: a03779c86df3323075f5e796cb2ce5029f00ec8869eee3fdfb897afe36c6d002
  directory: models
