# Esquemas de Control para PC Port de Pikmin

## Resumen de los 3 Modos de Control

### 1. **Modo Clásico (Classic)**
Comportamiento original de GameCube:
- **Gamepad**: Stick izquierdo controla cursor + movimiento de Olimar
- **Teclado**: WASD controla cursor + movimiento de Olimar
- **C-Stick**: TFGH / Stick derecho controla formación de Pikmin

### 2. **Modo Puntero Ratón (Mouse Cursor)**
El ratón controla el cursor/puntero independientemente:
- **Ratón**: Controla la dirección del cursor (alimenta valores al main stick)
- **Teclado WASD**: No se usa (o alternativa: control de cámara)
- **Gamepad**: Funciona en modo clásico si está conectado
- **C-Stick**: TFGH controla formación de Pikmin

### 3. **Modo Puntero + Olimar Ratón (Mouse Cursor + Olimar)**
El ratón controla tanto el cursor como el movimiento de Olimar:
- **Ratón**: 
  - Movimiento del ratón = dirección del cursor (main stick)
  - Click izquierdo sostenido = Olimar camina hacia el cursor
  - Click derecho = acciones contextuales
- **Teclado WASD**: Control alternativo de movimiento (opcional)
- **Gamepad**: Funciona en modo clásico si está conectado
- **C-Stick**: TFGH controla formación de Pikmin

## Implementación Técnica

### Variables de Estado
```cpp
enum PCControlMode {
    PC_CONTROL_CLASSIC = 0,      // Original GameCube behavior
    PC_CONTROL_MOUSE_CURSOR = 1,  // Mouse controls cursor only
    PC_CONTROL_MOUSE_OLIMAR = 2   // Mouse controls cursor + Olimar movement
};

static PCControlMode sControlMode = PC_CONTROL_CLASSIC;
```

### Lógica de Input

#### Modo Clásico
```cpp
// Keyboard/Gamepad stick → main stick (cursor + movement)
stickX = dirX * 127;
stickY = dirY * 127;
```

#### Modo Puntero Ratón
```cpp
// Mouse movement → main stick (cursor)
// WASD → disabled or camera control
stickX = mouseAccumX;
stickY = mouseAccumY;
```

#### Modo Puntero + Olimar Ratón
```cpp
// Mouse movement → main stick (cursor)
// Left click held → Olimar moves towards cursor
if (leftMouseHeld) {
    // Calculate direction from Olimar to cursor
    // Apply movement in that direction
    stickX = mouseAccumX;
    stickY = mouseAccumY;
}
```

## Interfaz de Usuario (Menú F1)

### Estructura del Menú
```
┌─ CONTROLES ──────────────────┐
│                              │
│  Esquema de Control:         │
│  ○ Clásico (GameCube)        │
│  ● Puntero Ratón             │
│  ○ Puntero + Olimar Ratón    │
│                              │
│  Sensibilidad Ratón: [▓▓▓░░]│
│                              │
│  [Tab] Toggle Ratón Relativo │
│                              │
└──────────────────────────────┘
```

### Controles del Menú
- **Flecha Arriba/Abajo**: Navegar opciones
- **Enter/A**: Seleccionar opción
- **F1**: Cerrar menú

## Archivos a Modificar

### 1. `pc_window.h`
```cpp
// Add control mode enum and functions
enum PCControlMode { ... };
void pc_window_set_control_mode(PCControlMode mode);
PCControlMode pc_window_get_control_mode(void);
void pc_window_set_mouse_sensitivity(float sensitivity);
float pc_window_get_mouse_sensitivity(void);
```

### 2. `pc_window.cpp`
```cpp
// Implement control mode switching
// Add logic for each mode in pc_window_poll_events()
```

### 3. `pc_settings.h/cpp` (si existe)
```cpp
// Add settings menu for F1 key
// Render control options UI
// Save/load control preferences
```

### 4. Configuración Persistente
```json
// settings.json or pikmin.cfg
{
    "controls": {
        "mode": "mouse_cursor",
        "mouse_sensitivity": 2.5,
        "mouse_relative_mode": true
    }
}
```

## Roadmap de Implementación

### Fase 1: Infraestructura Base ✓
- [x] Analizar sistema de cursor en navi.cpp
- [x] Implementar modo puntero ratón básico
- [x] Añadir toggle de modo relativo del ratón (Tab)

### Fase 2: Sistema de Modos de Control
- [ ] Crear enum PCControlMode
- [ ] Implementar switch entre modos
- [ ] Añadir modo "Puntero + Olimar Ratón"
- [ ] Probar cada modo individualmente

### Fase 3: Interfaz de Usuario (Menú F1)
- [ ] Crear sistema de menú overlay con F1
- [ ] Implementar UI para selección de modo
- [ ] Añadir slider de sensibilidad del ratón
- [ ] Añadir preview visual de cada modo

### Fase 4: Persistencia y Pulido
- [ ] Guardar/cargar preferencias de control
- [ ] Añadir tooltips y ayuda en el menú
- [ ] Implementar hotkeys para cambio rápido
- [ ] Pruebas exhaustivas con gamepad + teclado + ratón

### Fase 5: Refinamiento
- [ ] Ajustar curvas de sensibilidad
- [ ] Implementar deadzone configurable
- [ ] Añadir perfiles de control predefinidos
- [ ] Documentación de usuario

## Consideraciones de Diseño

### Compatibilidad con Gamepad
- Cuando un gamepad está conectado y se detecta input, cambiar automáticamente a modo clásico
- Permitir override manual desde el menú

### Transición Suave
- Al cambiar de modo, resetear valores acumulados del ratón
- Mostrar indicador visual temporal del modo actual

### Accesibilidad
- Permitir reasignar todas las teclas
- Soporte para múltiples dispositivos simultáneos
- Modo "entrenamiento" con tooltips en pantalla

## Notas de Desarrollo

### Modo Puntero + Olimar Ratón: Detalles Técnicos
Este modo requiere simular que el stick está siendo presionado continuamente hacia la dirección del cursor cuando el botón izquierdo del ratón está presionado:

```cpp
if (leftMouseButtonHeld && sControlMode == PC_CONTROL_MOUSE_OLIMAR) {
    // Mouse controls cursor direction
    stickX = mouseAccumX;
    stickY = mouseAccumY;
    
    // Normalize to ensure consistent movement speed
    float magnitude = sqrtf(stickX*stickX + stickY*stickY);
    if (magnitude > 127.0f) {
        stickX = (s8)(stickX * 127.0f / magnitude);
        stickY = (s8)(stickY * 127.0f / magnitude);
    }
}
```

### Testing Checklist
- [ ] Modo clásico con teclado
- [ ] Modo clásico con gamepad
- [ ] Modo puntero ratón (absoluto)
- [ ] Modo puntero ratón (relativo)
- [ ] Modo puntero + Olimar ratón
- [ ] Transición entre modos sin crashes
- [ ] Guardado/carga de preferencias
- [ ] UI del menú F1 responsive

## Referencias
- `src/plugPikiKando/navi.cpp:1710-1789` - Lógica de cursor original
- `pc_port/pc_window.cpp:288-564` - Input handling actual
- `src/sysDolphin/controllerMgr.cpp` - Mapeo de botones
