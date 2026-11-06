# Spectrum dev toolchain: z88dk (C + asm for +zx / +zxn, makes .tap and .nex)
# plus sjasmplus. Native arm64 and amd64 (no emulation needed).
#
#   scripts/build-toolchain-image.sh   builds this as zx-dev-toolchain:<N>
ARG Z88DK_IMAGE=z88dk/z88dk:sha-1ead955
FROM ${Z88DK_IMAGE} AS z88dk

FROM z88dk AS sjasm
ARG SJASMPLUS_REF=v1.24.0
RUN apk add --no-cache git g++ make cmake >/dev/null \
 && git clone -q --depth 1 --branch ${SJASMPLUS_REF} --recurse-submodules \
      https://github.com/z00m128/sjasmplus.git /tmp/sjasmplus \
 && make -C /tmp/sjasmplus -j"$(nproc)" >/dev/null \
 && install -m 755 /tmp/sjasmplus/sjasmplus /usr/local/bin/sjasmplus

FROM z88dk
COPY --from=sjasm /usr/local/bin/sjasmplus /usr/local/bin/sjasmplus
# g++ runtime for sjasmplus, make + python for project builds
RUN apk add --no-cache libstdc++ make python3 >/dev/null
WORKDIR /work
