# Contribuir

Gracias por mejorar Pikmin Native. Antes de empezar, revisa
[ROADMAP.md](ROADMAP.md), [AI_HANDOFF.md](AI_HANDOFF.md) y
[LEGAL.md](LEGAL.md).

## Flujo recomendado

1. Crea una rama pequeña y centrada en un solo problema.
2. Busca la causa estructural; evita excepciones por modelo, instrumento o nivel.
3. Compila en Release y ejecuta las pruebas offline aplicables.
4. Documenta qué cambió y cómo se verificó.
5. No marques una corrección visual o jugable como confirmada hasta probarla
   in-game con el recorrido descrito en [TEST_PLAN.md](TEST_PLAN.md).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

## Pull requests

Incluye una descripción breve del defecto, su causa y el criterio de prueba.
Indica distribución, GPU/controlador y resolución cuando afecte al render. Las
capturas y logs deben omitir rutas privadas si no son relevantes.

No adjuntes ni enlaces ROMs, assets extraídos o cualquier otro contenido del
juego. Los tests nuevos deben generar sus fixtures sintéticamente o trabajar
sólo con datos cuya redistribución esté autorizada.

Respeta los cambios ya presentes en el archivo que editas y evita reformas
masivas no relacionadas. Mantén actualizados `ROADMAP.md` y `AI_HANDOFF.md`
cuando el cambio altere el estado conocido del port.
