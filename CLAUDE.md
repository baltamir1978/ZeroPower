# ZeroPower

Ahorro de batería y ajustes de energía para el **M5Stack Cardputer Zero** (Raspberry Pi CM0,
512 MB, pantalla de 320×170, Debian 13 arm64). En el lanzador se llama **Power**. Qué hace y
cómo se usa está en `README.md`; aquí va lo que hace falta para tocarla.

No es una app de Android: de `~/CLAUDE.md` valen el estilo (comentarios, documentación y
commits en español explicando el porqué; pocas dependencias; lógica pura con pruebas), que el
repositorio es **público** (ni secretos ni datos personales) y la entrega; lo de Gradle, firma
y GrapheneOS no aplica. **La interfaz está en inglés** (va a la tienda), como ZeroGallery.

## Compilar y comprobar

```sh
Tools/compilar.sh pruebas   # simulador (SDL2) + ctest; tiene que pasar
Tools/compilar.sh deb       # dist/ZeroPower_<versión>_rev1_arm64.deb
Tools/compilar.sh simulador # abre el simulador en este escritorio (X11)
```

- `.cache` es un enlace a `~/.local/share/cardputer-zero-sdk` (LVGL y el *sysroot*, uno por máquina para todas las apps Zero; ver `~/Proyectos/CLAUDE.md`). En un clon nuevo: `mkdir -p ~/.local/share/cardputer-zero-sdk && ln -s ~/.local/share/cardputer-zero-sdk .cache`; si está vacío, la primera compilación lo descarga ahí.
- **Probar sin pantalla**: simulador bajo `Xvfb :99 -screen 0 900x600x24`
  (`SDL_RENDER_DRIVER=software`), teclas con `xdotool key --window`, captura de 320×170 con
  `import -window root -crop 320x170+287+118 +repage`. Sin el ayudante instalado la app simula
  el sistema en memoria (`PowerControl::simulated`). Para no tocar tu `~/.config`, lanzar con
  `XDG_CONFIG_HOME=<carpeta>`.
- **El aparato** es `cpzero` en Tailscale. Se mide el consumo con
  `/sys/class/power_supply/bq27220-0/current_now` (µA, **negativo al descargar**), media de
  30 s, desenchufado.

## De dónde viene

- **Base**: ZeroGPS (que viene de la plantilla oficial), con sus arreglos: el bucle de LVGL que
  duerme de verdad (`lv_delay_set_cb`), todas las letras en evdev y `set_page_handlers(owner…)`.
- **Ideas y medidas**: [cz-battery-saver](https://github.com/InstantComet/cz-battery-saver)
  (MIT) — pausar el lanzador con la pantalla apagada, aparcar su audio, Ethernet USB en
  auto-suspend, apagado con batería baja. **El código es propio**; se le da crédito en el README
  y en la ficha. El gist de optimización de TheKiwil **no tiene licencia: no se copia nada de
  él** (lo que coincide es configuración estándar de Debian/Raspberry Pi).

## Cómo está hecha

- **`src/model/`** (puro, con `tests/power_model_test.cpp`): `power_settings` (catálogo de
  ajustes con título, ayuda, opciones y quién los aplica; `key=value`; opciones del servicio)
  y `power_logic` (media del consumo, horas que quedan, `LowBatteryGuard`, `IdleTracker`,
  `ScreenTimeout`).
- **Ajustes del sistema**: el sistema es la fuente de verdad. `packaging/zeropower-helper`
  (bash, se instala en `/usr/lib/zeropower/`): `status` sin root; `set <ajuste> <valor>` y
  `restore` con root, por `sudo -n` con el sudo sin contraseña que trae el usuario del aparato
  (`bruno ALL=(ALL) NOPASSWD:ALL` en `90-cloud-init-users`, del Imager). Si sudo pide
  contraseña (`classify_sudo` en `src/model/sudo_password.h`), `SettingsScreen` la pide encima
  de la lista y `PowerControl::apply` repite con `sudo -S -p ""`, pasándola por la entrada
  estándar de `run_process` y borrándola al acabar (`SecretInput`, `wipe`). En el simulador se
  prueba con `ZEROPOWER_SIMULATE_SUDO=<clave>`. Para teclearla, `linux_input` entiende Shift
  y la capa Sym del Cardputer (lee `/usr/share/keymaps/tca8418_keypad_m5stack_keymap.map`), y
  la T no cambia el tema mientras se escribe (`set_text_entry`). **La tienda no deja
  instalar sudoers** (ni en `/etc/sudoers.d` ni desde los scripts: busca `/etc/sudoers`), ni
  polkit. Guarda el original de cada ajuste en `/var/lib/zeropower/original/` y `restore`
  (en el `prerm`) lo devuelve. **No hay servicio de root al arrancar** (la tienda rechaza
  servicios de root): todo queda puesto en el sistema (servicios, reglas de udev
  `70-zeropower-ethernet.rules` y `70-zeropower-leds.rules`, `config.txt`, la radio del wifi,
  un *drop-in* de journald). Las salidas de 5 V (`grove_5v_out`, `ext_5v_out`) y el LED `ACT`
  son LEDs en `/sys/class/leds`; `restore` deja las de 5 V como estén y sólo quita su regla.
  `cpu_max` escribe `scaling_max_freq` ya y `arm_freq` en `config.txt` para el arranque.
  Los servicios van de uno en uno (`SERVICE_KEYS`, cada uno con sus unidades en
  `service_units`); `extra_services` (todos a la vez, de la 0.1.3) ya no sale en `status`,
  pero se acepta y `restore` lo deshace (`RESTORE_KEYS`).
- **`zeropowerd`** (`src/daemon/`): servicio **del sistema** en `/usr/lib/systemd/system/`
  con `User=1000` y `XDG_RUNTIME_DIR=/run/user/1000` (la tienda sólo admite unidades ahí, con
  el nombre del paquete y que no corran como root; una unidad de usuario en
  `/usr/lib/systemd/user` la rechaza). Apagar: `systemctl poweroff` y, si polkit no lo deja
  desde un servicio, `sudo -n`.
  Opciones en `~/.config/zeropower/power.conf` (la app lo reescribe con temporal + rename; el
  servicio mira el `mtime` cada 3 s). Duerme en `poll()` sobre el teclado (1 s de tope).
  Pantalla apagada = `/sys/class/backlight/backlight/brightness` a 0 durante `idle_delay` s →
  `pactl` mueve los flujos del lanzador a un `module-null-sink` (`idle_audio`) y `SIGSTOP` al
  lanzador (`idle_launcher`; el antiguo `idle_saver` aún se lee) (no si tiene
  hijos vivos: una app abierta; los zombis no cuentan). Tecla o brillo > 0 → `SIGCONT` y el
  audio vuelve. Con la pantalla encendida y `screen_timeout` s sin teclas, el servicio escribe
  brillo 0 (el usuario está en el grupo `video`) y la tecla siguiente devuelve el que había.
  Al pararse lo deja todo como estaba (salvo al apagar por batería: no enciende la pantalla).
- **App**: `SettingsScreen` es la misma lista para los tres grupos (Hardware, Services,
  Power saving): 5 filas a la vista y, con más, se desplaza con una barra a la derecha; la
  pista añade *Needs restart* a los ajustes con `needs_reboot` (hoy sólo `gpu_mem`). Lee y
  aplica en un hilo (`PowerControl`). `BatteryScreen`: media de 30 s y
  barras del último minuto.
- `platform::run_process` lanza sin shell (lista de argumentos).

## Hardware y medidas (cpzero, 20-nov-2026)

- Pantalla interna: panel SPI `panel-mipi-dbi` (`card1-SPI-1`), 320×170 RGB565: no usa la
  GPU. `gpu_mem=64` de fábrica; 16 basta para la pantalla, pero con 16 el firmware es el
  recortado (`start_cd`) y no hay decodificador de vídeo por hardware: ZeroGallery (0.6.1) lo usa
  para mandar vídeo al HDMI si está, y **necesita 64**: con 32 el decodificador aparece pero el
  firmware no puede crearlo («Not enough GPU mem?», probado el 25-nov-2026). Bruno lo prefiere y `cpzero` volvió a 64 el 25-nov-2026
  (puesto desde la propia app y reiniciado).
- Ethernet USB interno: `0fe6:9900` (SR9900A) en `1-1.1`, detrás de un hub Genesys.
- Bluetooth por UART (`hci0`); la radio ya venía bloqueada por rfkill.
- Consumo en reposo, desenchufado: **378 mA** con pantalla y lanzador (brillo 100 %, wifi);
  con la pantalla apagada y `zeropowerd` (lanzador en pausa, audio aparcado) **301 mA**;
  **+ Ethernet auto-suspend 209 mA**; Ethernet off, escritorio HDMI off y Bluetooth off no
  bajan más el consumo (sí liberan RAM: de 235 a 221 MB usados).

## Versiones y entrega

- Versión en `project(... VERSION x.y.z)` de `CMakeLists.txt` y en `app-builder.json`, a la vez.
- El `.deb` se copia a `~/Documentos/Software/ZeroPower-<versión>.deb`, sustituyendo al anterior.
- Commit en `main` con mensaje en español que empieza por la versión, y push.
- Tienda: `../AppBuilder/czdev publish --deb dist/…` (ver la memoria `cardputer-zero-apps`);
  la primera publicación pide un vídeo en el aparato (**sin metadatos de ubicación**).
  Icono: `Tools/generar_icono.py`.

## El interruptor USB (Host/Slave)

Medido el 24-nov-2026 en `cpzero`, desenchufado, con la **pantalla apagada y el lanzador en
pausa** por ZeroPower y sus ajustes puestos (media de un minuto de `current_now`). Para saber si
la pantalla está apagada hay que mirar `brightness` (ZeroPower la apaga poniéndola a 0), no
`bl_power`, que sigue en 0:

| Interruptor | Consumo | Interrupciones USB/s |
|---|---|---|
| Host (hub encendido) | 286 mA | 8028 |
| Slave (hub desconectado) | 198 mA | 0 |

En Slave se desconecta el hub GL852G y con él el chip Ethernet USB (0fe6:9900), el USB-A, el
USB-C de salida y el USB de los Cap: el controlador del CM0 se queda sin dispositivos y deja de
despertar al procesador 8000 veces por segundo. Son 88 mA menos (31 %), unas dos horas y media
más de batería. La carga funciona igual en las dos posiciones. Por software se sabe la
posición: en Slave `lsusb` sólo ve el root hub. Desde la 0.1.4 la pantalla *Battery* lo
enseña (`power::usb_switch_state`: Host si en `/sys/bus/usb/devices` hay algo que no sea un root
hub `1d6b`).

## Estado

- **0.1.4 (24-nov-2026)**: indicador del interruptor USB en *Battery* (amarillo en Host, verde
  en Slave) y su explicación en la ayuda y el README. Instalada en cpzero y vista en el aparato
  (en Slave). Bruno la da por buena (25-nov-2026: «zero power está ok»). **Enviada a la tienda:
  [#796](https://github.com/CardputerZero/packages/pull/796).**
- **Probada en el aparato (24-nov-2026):** Bruno la da por buena («zeropower ok»). La 0.1.3 está en la tienda ([#675](https://github.com/CardputerZero/packages/pull/675), fusionado).

- **0.1.3 (22-nov-2026) — *Screen timeout*.** El Cardputer se quedó sin batería: a las 16:14
  algo lo despertó y durante una hora el lanzador siguió despierto sin que `zeropowerd` lo
  volviera a pausar (no hay más «screen off» en el registro). Reproducido: si la pantalla se
  enciende **sin tecla** mientras el salvapantallas del lanzador está activo, el lanzador
  sigue en su estado `Locked` creyendo que está a oscuras y **no la vuelve a apagar nunca**
  (su `LockscreenStateModel::poll` no hace nada en `Locked`). Arreglo: `ScreenTimeout`
  (en `power_logic`) apaga la pantalla tras `screen_timeout` s sin teclas (por defecto 300) y
  la tecla siguiente le devuelve el brillo; si otro la enciende, deja de ser nuestra. Además
  `open_app()` ignora hijos zombis (un `nmcli` terminado con el lanzador pausado parecía una
  app abierta) y el registro dice por qué despierta (`wake: key / screen turned on`) y qué app
  hay abierta. Probado en el aparato: pantalla encendida a mano → apagada al minuto; tecla →
  encendida. Instalada en cpzero. **Enviada a la tienda: [#675](https://github.com/CardputerZero/packages/pull/675)**
  (sustituye al #572 de la 0.1.2, cerrado el 22-nov-2026); CI en verde y
  el vídeo de la 0.1.1 enlazado en un comentario; espera revisión de un responsable. Nueva opción «Screen timeout» en Power saving (1/2/5/10 min
  o Never; captura `screenshot/saving.png` rehecha). Límite conocido: la tecla que la
  despierta también le llega al lanzador o a la app. Pendiente: avisar en pantalla antes del
  apagado por batería baja (al «apagar», el CM0 se detiene pero la placa sigue con corriente
  y parece colgado).
- **0.1.2 (20-nov-2026) — lo que pide la tienda.** El primer envío (0.1.1, PR #571) lo rechazó
  la validación de rutas: `/etc/sudoers.d/zeropower` y `usr/lib/systemd/user/zeropowerd.service`.
  Fuera el sudoers (se usa el sudo sin contraseña del usuario) y el servicio pasa a ser de
  sistema con `User=1000`. Reglas: `.github/scripts/install_path_policy.py` de
  `CardputerZero/packages`. Instalada en el aparato (servicio como `bruno`, sudoers fuera).
  **Enviada a la tienda: [#572](https://github.com/CardputerZero/packages/pull/572)**, CI en
  verde, esperando revisión; vídeo en la release `video-0.1.1` (sin ubicación ni audio). #571
  cerrado.

- **0.1.1 (20-nov-2026) — interruptores.** Los ajustes de sí/no son un `lv_switch` al estilo
  de iOS (verde `#34C759`, sin animación), que `ENTER`, `Z` o `C` cambian y aplican al momento
  (lo pidió Bruno: «más fácil un interruptor estilo Apple»). Mientras se aplica ya enseña el
  valor nuevo; si falla, vuelve. Apagar Wi-Fi o Tailscale lo pone ámbar y pide otra pulsación.
  Los de varias opciones siguen con `‹ valor ›` + `ENTER`. Probado en el simulador.

- **0.1.0 (20-nov-2026) — primera versión; instalada en el aparato.**
  - Probada en el simulador (las cuatro pantallas, elegir y aplicar, la confirmación de
    Tailscale/Wi-Fi) y, en el aparato, el ayudante (`status`, `set` por sudo sin contraseña),
    el sudoers (`visudo -c`) y `zeropowerd` (pausa el lanzador con la pantalla apagada; 0,5 %
    de CPU, 3 MB). Con el ayudante se dejaron puestos, a petición de Bruno, **Ethernet
    auto-suspend → off, escritorio HDMI off y Bluetooth off**.
  - Entonces sin probar en el aparato (Bruno lo da todo por bueno el 25-nov-2026): la app en la pantalla del Cardputer, `gpu_mem` (pide
    reiniciar), Wi-Fi/Tailscale desde la app, el apagado por batería baja y `restore` al
    desinstalar.
