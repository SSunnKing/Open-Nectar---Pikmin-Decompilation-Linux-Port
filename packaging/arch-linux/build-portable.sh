#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"
build_dir="${repo_root}/build-arch-linux"
output_dir="${script_dir}/out/pikmin-native-linux-arch"
stage_dir="${build_dir}/stage"
configure_log="${build_dir}/configure.log"
build_log="${build_dir}/build.log"
test_log="${build_dir}/test.log"

usage() {
    printf '%s\n' \
        "Uso: $0 [--clean] [--skip-tests]" \
        "" \
        "Genera una carpeta portable para Arch Linux en:" \
        "  packaging/arch-linux/out/pikmin-native-linux-arch"
}

clean=0
run_tests=1
while (($#)); do
    case "$1" in
        --clean) clean=1 ;;
        --skip-tests) run_tests=0 ;;
        --help|-h) usage; exit 0 ;;
        *) printf 'Opción desconocida: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

required_commands=(cmake c++ pkg-config)
for command_name in "${required_commands[@]}"; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        printf 'Falta el comando requerido: %s\n' "${command_name}" >&2
        printf '%s\n' 'Instala las dependencias indicadas en packaging/arch-linux/README.md.' >&2
        exit 1
    fi
done

if ! pkg-config --exists sdl2; then
    printf '%s\n' 'No se encontró SDL2 mediante pkg-config (paquete Arch: sdl2).' >&2
    exit 1
fi

if ((clean)); then
    cmake -E remove_directory "${build_dir}"
    cmake -E remove_directory "${output_dir}"
fi

cmake -E make_directory "${build_dir}"
printf '%s\n' '[1/4] Configurando CMake...'
if ! cmake -S "${repo_root}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DPIKMIN_NATIVE_OPTIMIZE=OFF \
    -DPIKMIN_ENABLE_IPO=OFF \
    -DCMAKE_INSTALL_PREFIX=/usr >"${configure_log}" 2>&1; then
    printf 'Falló la configuración. Últimas líneas de %s:\n' "${configure_log}" >&2
    tail -n 80 "${configure_log}" >&2
    exit 1
fi

printf '%s\n' '[2/4] Compilando juego y launcher (los avisos quedan en build.log)...'
if ! cmake --build "${build_dir}" --parallel "$(nproc)" >"${build_log}" 2>&1; then
    printf 'Falló la compilación. Últimas líneas de %s:\n' "${build_log}" >&2
    tail -n 120 "${build_log}" >&2
    exit 1
fi

if ((run_tests)); then
    printf '%s\n' '[3/4] Ejecutando pruebas offline...'
    if ! ctest --test-dir "${build_dir}" --output-on-failure >"${test_log}" 2>&1; then
        printf 'Fallaron las pruebas. Contenido de %s:\n' "${test_log}" >&2
        cat "${test_log}" >&2
        exit 1
    fi
else
    printf '%s\n' '[3/4] Pruebas omitidas por --skip-tests.'
fi

printf '%s\n' '[4/4] Creando carpeta portable...'
cmake -E remove_directory "${stage_dir}"
DESTDIR="${stage_dir}" cmake --install "${build_dir}" --strip

cmake -E remove_directory "${output_dir}"
cmake -E make_directory "${output_dir}"
cmake -E copy "${stage_dir}/usr/bin/pikmin" "${output_dir}/pikmin"
cmake -E copy "${stage_dir}/usr/bin/pikmin-launcher" "${output_dir}/pikmin-launcher"
cmake -E copy "${script_dir}/PORTABLE_README.txt" "${output_dir}/LEEME.txt"

chmod 755 "${output_dir}/pikmin" "${output_dir}/pikmin-launcher"

"${output_dir}/pikmin-launcher" --help >/dev/null

printf '\nPaquete portable creado correctamente:\n  %s\n' "${output_dir}"
printf '%s\n' 'Copia esa carpeta completa al equipo Arch y ejecuta pikmin-launcher.'
