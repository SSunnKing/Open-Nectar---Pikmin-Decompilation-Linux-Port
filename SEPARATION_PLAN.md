# Plan de Separación: Cursor vs Movimiento de Olimar

## Análisis de la Arquitectura Actual

### Flujo de Control Actual
```
PAD Input (stickX, stickY)
    ↓
Controller::mMainStickX, mMainStickY
    ↓
Navi::makeVelocity() lee mKontroller->getMainStickX/Y()
    ↓
stickVec usado para:
    1. mMainStick (movimiento de Olimar) - línea 1728
    2. mCursorPosition (cursor) - líneas 1777-1789
```

### Archivos Afectados (12 total)

**CORE (modificar):**
1. `include/Controller.h` - Añadir mCursorStickX/Y
2. `src/sysCommon/controller.cpp` - Añadir getCursorStickX/Y()
3. `include/Kontroller.h` - Sin cambios (hereda de Controller)
4. `src/plugPikiKando/kontroller.cpp` - Guardar/cargar cursor stick
5. `src/sysDolphin/controllerMgr.cpp` - Leer cursor stick desde PAD
6. `src/plugPikiKando/navi.cpp` - Usar cursor stick para cursor

**READ-ONLY (solo lectura, verificar):**
7. `src/plugPikiKando/naviState.cpp` - Usa mMainStick para estados
8. `src/plugPikiKando/pikiState.cpp` - Lee mMainStick
9. `src/plugPikiKando/gmWin.cpp` - Usa getMainStickY() para menús
10. `src/plugPikiKando/seMgr.cpp` - Usa getMainStickY() para sonido
11. `src/plugPikiColin/titles.cpp` - Usa mMainStickX/Y para títulos
12. `src/plugPikiNakata/panitestsection.cpp` - Test code

## Plan de Modificación (Fase por Fase)

### FASE 0: Backup (CRÍTICO)
```bash
cd pikmin
mkdir -p backups/$(date +%Y%m%d_%H%M%S)
cp -r include/Controller.h backups/$(date +%Y%m%d_%H%M%S)/
cp -r include/Kontroller.h backups/$(date +%Y%m%d_%H%M%S)/
cp -r src/sysCommon/controller.cpp backups/$(date +%Y%m%d_%H%M%S)/
cp -r src/plugPikiKando/kontroller.cpp backups/$(date +%Y%m%d_%H%M%S)/
cp -r src/plugPikiKando/navi.cpp backups/$(date +%Y%m%d_%H%M%S)/
cp -r src/sysDolphin/controllerMgr.cpp backups/$(date +%Y%m%d_%H%M%S)/
```

### FASE 1: Añadir Variables al Controller (include/Controller.h)

**Antes:**
```cpp
s8 mMainStickX;    // _45
s8 mMainStickY;    // _46
s8 mSubStickX;     // _47
s8 mSubStickY;     // _48
```

**Después:**
```cpp
s8 mMainStickX;    // _45 - Movement only
s8 mMainStickY;    // _46 - Movement only
s8 mSubStickX;     // _47 - Pikmin formation (C-stick)
s8 mSubStickY;     // _48 - Pikmin formation (C-stick)
s8 mCursorStickX;  // _4D - Cursor control (PC mouse/keyboard)
s8 mCursorStickY;  // _4E - Cursor control (PC mouse/keyboard)
```

**RIESGO:** Cambia el tamaño de la estructura Controller
- Offset de miembros posteriores cambia
- **SOLUCIÓN:** Verificar que no hay miembros después de _4C (mTriggerR)
- Controller termina en _4C, siguiente clase comienza en nuevo offset

### FASE 2: Añadir Getters (include/Controller.h + src/sysCommon/controller.cpp)

**Controller.h:**
```cpp
f32 getMainStickX();    // Movement
f32 getMainStickY();    // Movement
f32 getSubStickX();     // C-stick
f32 getSubStickY();     // C-stick
f32 getCursorStickX();  // NUEVO - Cursor
f32 getCursorStickY();  // NUEVO - Cursor
```

**controller.cpp:**
```cpp
f32 Controller::getCursorStickX() {
    return mCursorStickX / 74.0f;
}

f32 Controller::getCursorStickY() {
    return mCursorStickY / 74.0f;
}
```

### FASE 3: Input desde PAD (src/sysDolphin/controllerMgr.cpp)

**Actual (línea 62-65):**
```cpp
controller->mMainStickX = sControllerPad[controller->mPlayerNum - 1].stickX;
controller->mMainStickY = sControllerPad[controller->mPlayerNum - 1].stickY;
controller->mSubStickX  = sControllerPad[controller->mPlayerNum - 1].substickX;
controller->mSubStickY  = sControllerPad[controller->mPlayerNum - 1].substickY;
```

**Después:**
```cpp
controller->mMainStickX = sControllerPad[controller->mPlayerNum - 1].stickX;
controller->mMainStickY = sControllerPad[controller->mPlayerNum - 1].stickY;
controller->mSubStickX  = sControllerPad[controller->mPlayerNum - 1].substickX;
controller->mSubStickY  = sControllerPad[controller->mPlayerNum - 1].substickY;

// PC Port: Use stick for cursor in classic mode, mouse in mouse modes
#ifdef PIKI_PC_PORT
if (pc_window_get_control_mode() == PC_CONTROL_CLASSIC) {
    // Classic: cursor follows movement stick
    controller->mCursorStickX = sControllerPad[controller->mPlayerNum - 1].stickX;
    controller->mCursorStickY = sControllerPad[controller->mPlayerNum - 1].stickY;
} else {
    // Mouse modes: use separate cursor stick (populated by pc_window.cpp)
    controller->mCursorStickX = sControllerPad[controller->mPlayerNum - 1].cursorStickX;
    controller->mCursorStickY = sControllerPad[controller->mPlayerNum - 1].cursorStickY;
}
#else
// Original behavior: cursor = movement stick
controller->mCursorStickX = sControllerPad[controller->mPlayerNum - 1].stickX;
controller->mCursorStickY = sControllerPad[controller->mPlayerNum - 1].stickY;
#endif
```

**PROBLEMA:** PADStatus no tiene cursorStickX/Y

**SOLUCIÓN ALTERNATIVA:** Usar substick temporal como cursor stick en modos mouse
```cpp
#ifdef PIKI_PC_PORT
if (pc_window_get_control_mode() != PC_CONTROL_CLASSIC) {
    // In mouse modes: substick becomes cursor stick
    controller->mCursorStickX = sControllerPad[controller->mPlayerNum - 1].substickX;
    controller->mCursorStickY = sControllerPad[controller->mPlayerNum - 1].substickY;
} else {
    controller->mCursorStickX = sControllerPad[controller->mPlayerNum - 1].stickX;
    controller->mCursorStickY = sControllerPad[controller->mPlayerNum - 1].stickY;
}
#endif
```

### FASE 4: Modificar Navi para Usar Cursor Stick (src/plugPikiKando/navi.cpp)

**Línea 1710 - Antes:**
```cpp
NVector3f stickVec(mKontroller->getMainStickX(), 0.0f, -mKontroller->getMainStickY());
```

**Línea 1710 - Después:**
```cpp
// Main stick for movement
NVector3f stickVec(mKontroller->getMainStickX(), 0.0f, -mKontroller->getMainStickY());
```

**Línea 1775 - Antes:**
```cpp
Vector3f stickVec2(stickVec);
stickVec2.normalise();
stickVec2 = stickVec2 * NAVI_PARM(mCursorMoveSpeed);
```

**Línea 1775 - Después:**
```cpp
// Cursor stick for cursor position (separate from movement)
#ifdef PIKI_PC_PORT
NVector3f cursorStickVec(mKontroller->getCursorStickX(), 0.0f, -mKontroller->getCursorStickY());
Vector3f stickVec2(cursorStickVec);
#else
Vector3f stickVec2(stickVec);  // Original: cursor follows movement
#endif
stickVec2.normalise();
stickVec2 = stickVec2 * NAVI_PARM(mCursorMoveSpeed);
```

### FASE 5: Serialización (src/plugPikiKando/kontroller.cpp)

**write() - Línea 128:**
```cpp
stream.writeByte(mMainStickX);
stream.writeByte(mMainStickY);
stream.writeByte(mSubStickX);
stream.writeByte(mSubStickY);
stream.writeByte(mCursorStickX);  // NUEVO
stream.writeByte(mCursorStickY);  // NUEVO
```

**read() - Línea 144:**
```cpp
mMainStickX   = stream.readByte();
mMainStickY   = stream.readByte();
mSubStickX    = stream.readByte();
mSubStickY    = stream.readByte();
mCursorStickX = stream.readByte();  // NUEVO
mCursorStickY = stream.readByte();  // NUEVO
```

**getSaveSize() - Línea 119:**
```cpp
return duration * 14;  // Era 12, ahora 14 (2 bytes más)
```

## Verificación de Cambios

### Archivos que NO requieren modificación (solo leen mMainStick):
- `naviState.cpp` - Solo usa mMainStick para movimiento ✓
- `pikiState.cpp` - Solo usa mMainStick para movimiento ✓
- `gmWin.cpp` - Solo usa MainStick para menús ✓
- `seMgr.cpp` - Solo usa MainStick para audio ✓
- `titles.cpp` - Solo usa MainStick para títulos ✓

## Testing Checklist

### Test 1: Compilación
- [ ] Proyecto compila sin errores
- [ ] No hay warnings críticos sobre tamaños de estructura

### Test 2: Modo Clásico (PC_CONTROL_CLASSIC)
- [ ] WASD mueve a Olimar
- [ ] WASD mueve el cursor
- [ ] Gamepad stick mueve ambos

### Test 3: Modo Puntero Ratón (PC_CONTROL_MOUSE_CURSOR)
- [ ] Ratón mueve solo el cursor
- [ ] WASD NO mueve a Olimar (deshabilitado)
- [ ] Gamepad stick sigue moviendo ambos (fallback)

### Test 4: Modo Puntero Olimar (PC_CONTROL_MOUSE_OLIMAR)
- [ ] Ratón mueve el cursor
- [ ] Olimar mira hacia el cursor
- [ ] (Futuro) Click izq = camina hacia cursor

### Test 5: Serialización
- [ ] Guardar/cargar replay funciona
- [ ] No hay corrupción de datos

## Rollback Plan

Si algo falla:
```bash
cd pikmin
BACKUP_DIR=$(ls -t backups/ | head -1)
cp backups/$BACKUP_DIR/* ./ -r
```

## Riesgos Identificados

| Riesgo | Severidad | Mitigación |
|--------|-----------|------------|
| Cambio de tamaño de Controller | ALTA | Verificar offsets, Controller no tiene herencia múltiple |
| Romper replays existentes | MEDIA | Versionar formato, añadir compatibilidad |
| Gamepad sin cursor stick | BAJA | Fallback a modo clásico |
| Cambios en otros sistemas | BAJA | Solo Navi usa cursor |

## Decisión Final

**PREGUNTA:** ¿Procedo con la implementación?

**ALTERNATIVA MÁS SEGURA:** 
En lugar de modificar Controller, crear un sistema de "cursor virtual" en pc_window.cpp que:
1. En modo mouse: substick = cursor (pc_window lo alimenta)
2. En modo clásico: substick = formación, stick = ambos
3. Navi lee substick como cursor en modos PC

Esto NO requiere modificar la estructura Controller, solo la lógica de input en pc_window.cpp

¿Cuál prefieres?
A) Modificación completa (separación real, más invasiva)
B) Alternativa segura (reutilizar substick, menos cambios)
