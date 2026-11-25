# ZeroPower

Ahorro de batería y ajustes de energía para el [M5Stack Cardputer Zero](https://docs.m5stack.com/)
(Raspberry Pi CM0, 512 MB, pantalla de 320×170, Debian 13). En el lanzador se llama **Power**.

El Cardputer Zero gasta unos **380-420 mA** sin hacer nada: con su batería de 1.750 mAh son
poco más de 4 horas. Buena parte se va en cosas que en un aparato de bolsillo no se usan (el
chip Ethernet USB interno, el escritorio para una pantalla HDMI, el Bluetooth, servicios de
red) y en el lanzador, que sigue funcionando y con el audio abierto aunque la pantalla esté
apagada. ZeroPower deja apagar cada cosa **para siempre** desde una pantalla, ver al momento
cuánto consume el aparato y **deshacerlo** todo igual de fácil.

## Qué hace

- **Battery**: el consumo de ahora (media de los últimos 30 s, del medidor de la batería
  bq27220), la carga, el voltaje y el tiempo que queda a ese ritmo, con el último minuto en
  barras. `R` empieza la media de cero: así se ve lo que ahorra cada ajuste. Debajo, **en qué
  posición está el interruptor USB** del lateral (ver más abajo): en amarillo si está en
  *Host*, que gasta más; en verde si está en *Slave*.
- **Hardware**:
  - **USB Ethernet**: *On*, *Auto-suspend* (el chip duerme si no hay cable: ~100 mA menos) u
    *Off* (desactivado).
  - **Wi-Fi**: encendido o apagado (sin red, ni tienda, ni acceso remoto: pide confirmar).
  - **Bluetooth**: radio y servicio.
  - **HDMI desktop**: el escritorio para una pantalla HDMI (`lightdm`); apagado libera RAM y
    CPU. El lanzador no depende de él.
  - **Desk Mode unplug** (sólo con [Desk Mode](https://github.com/InstantComet/cz-desk-mode)):
    cierra su escritorio HDMI cuando el HDMI lleva 1, 5, 15 o 30 minutos desconectado (~55 MB
    de RAM). Lo que no se haya guardado en él se pierde: por defecto, *Keep desktop*.
  - **GPU memory**: la RAM que se reserva para el firmware de la GPU. La pantalla interna es
    un panel SPI que no la usa: con **16 MB** quedan 48 MB más para Linux. Vale tras reiniciar
    (la pista lo dice: *Needs restart*); es el único ajuste que lo necesita.
  - **CPU max speed**: 1000 (de fábrica), 800 o 600 MHz. Vale ya mismo y tras reiniciar
    (`arm_freq` en `config.txt`). Las apps van más lentas, pero los ratos de mucho trabajo
    gastan menos; en reposo, a 600 MHz, sólo ~5 mA.
  - **Status LED**: el LED verde de actividad del módulo CM0 (~4 mA).
  - **Grove 5V** y **GPIO 5V**: los 5 V del puerto Grove y de los pines GPIO (para los Caps).
    Al arrancar están apagados; una app para un Cap puede encender el de GPIO por su cuenta.
    Si se encienden aquí, se quedan encendidos también tras reiniciar. Sin nada enchufado
    gastan ~3 mA cada uno.
- **Services**, cada uno con su interruptor:
  - **Tailscale** (pide confirmar).
  - **NFS client**: `rpcbind` y `nfs-blkmap`, para carpetas compartidas por red.
  - **Avahi (.local)**: el nombre `equipo.local` en la red; sin él, se entra por la IP.
  - **Daily apt updates**: los temporizadores `apt-daily` y `apt-daily-upgrade`; `apt` a mano
    sigue funcionando.
  - **Man page index**: el temporizador diario de `man-db`.
  - **Disk scrub timer**: `e2scrub_all`, que sólo revisa volúmenes LVM (la tarjeta no tiene).
  - **Logs in RAM**: el journal en memoria (menos escrituras en la tarjeta).
- **Power saving** (lo hace el servicio `zeropowerd`):
  - **Sleep sound card**: con la pantalla apagada, aparca el audio del lanzador para que
    duerman la tarjeta de sonido y el amplificador (~40 mA).
  - **Pause launcher**: con la pantalla apagada, pausa el lanzador, que no deja de repintar
    (~30 mA). Una tecla lo despierta. Si hay una app abierta no pausa nada.
  - **Saver delay**: cuánto tiene que llevar apagada la pantalla para los dos anteriores (3–60 s).
  - **Quiet launcher**: el lanzador sin sonido de teclas, siempre.
  - **Low battery off**: apaga ordenadamente al 5, 8, 10 o 15 % (tres lecturas seguidas).
  - **Screen timeout**: si la pantalla sigue encendida tras 1, 2, 5 (por defecto) o 10 minutos
    sin tocar una tecla, la apaga el propio servicio; la siguiente tecla la enciende. Es la red
    de seguridad para cuando el lanzador no la apaga (también dentro de las apps: si ves vídeos
    largos sin tocar nada, súbelo o ponlo en *Never*).

Cada ajuste se queda puesto en el propio sistema (servicios desactivados, una regla de `udev`,
`config.txt`, la radio del wifi), así que sobrevive a los reinicios sin que nada tenga que
volver a aplicarlo. **Al desinstalar ZeroPower, todo lo que cambió vuelve a como estaba.**

## El interruptor USB del lateral

El interruptor del lateral izquierdo (*Host* / *Slave*) no se puede mover por software, pero
cuenta mucho: en **Host** está encendido el hub USB interno, y con él el chip de red por cable,
el USB-A, el USB-C de salida y el USB de los Cap; en **Slave** el hub se desconecta y el
procesador deja de despertarse 8000 veces por segundo para atenderlo.

| Interruptor | Consumo (pantalla apagada, con ZeroPower) |
|---|---|
| Host | 286 mA |
| Slave | **198 mA** (−88 mA, −31 %: unas dos horas y media más) |

Si no usas nada por USB (pinchos, ratón, un receptor de radio, la red por cable), déjalo en
**Slave**. El Wi-Fi, el Bluetooth, la carga y los Cap que van por los pines del bus (SPI, I²C,
UART: LoRa, GPS, CC1101, NFC…) funcionan igual. ZeroPower lo enseña en la pantalla *Battery*.

## Teclas

| Dónde | Tecla | Qué hace |
|---|---|---|
| Todas | `Fn`+`H` · `Fn`+`J` · `T` | Ayuda · captura · claro/oscuro |
| | `Esc` / mantener `Esc` | Atrás / salir |
| Menú | `F` / `X` · `ENTER` | Elegir · abrir |
| Ajustes | `F` / `X` | Elegir ajuste |
| | `ENTER` (o `Z` / `C`) en un interruptor | Encender / apagar al momento (Wi-Fi y Tailscale piden pulsar otra vez para apagar) |
| | `Z` / `C` en uno de varias opciones | Elegir valor (queda marcado con `•`) |
| | `ENTER` | Aplicar el valor elegido (sin elegir, pasa al siguiente) |
| | `Esc` | Descartar lo elegido / volver |
| Contraseña | letras, `Shift`, `Sym` · `ENTER` · `Esc` | Escribirla · confirmar · no cambiar nada |
| Battery | `R` | Empezar la media de cero |

## Cómo funciona

- **La app** corre como tu usuario. Lee el estado real con
  `/usr/lib/zeropower/zeropower-helper status` (sin root).
- **Para cambiar algo** llama a `sudo -n /usr/lib/zeropower/zeropower-helper set <ajuste> <valor>`,
  con el `sudo` sin contraseña que el usuario del aparato ya trae de fábrica (lo pone el
  Raspberry Pi Imager). Si tu usuario necesita contraseña para sudo, la app la pide en
  pantalla y repite el cambio con `sudo -S`: la contraseña va por la entrada estándar (nunca
  en la línea de órdenes), no se guarda y se borra de la memoria al acabar; sudo la recuerda
  unos minutos, como en un terminal. ZeroPower **no instala ningún fichero de sudoers** ni da
  permisos nuevos; un usuario sin sudo no puede cambiar nada. El ayudante es un script de
  bash corto que acepta una lista cerrada de ajustes y valores; la primera vez que cambia
  algo guarda su valor original en `/var/lib/zeropower/original/` para poder devolverlo
  (`zeropower-helper restore`, que se ejecuta al desinstalar).
- **`zeropowerd`** es un servicio del sistema que corre **como el usuario del aparato**
  (`User=1000`, el del lanzador), no como root. Lee sus opciones de
  `~/.config/zeropower/power.conf`, que la app reescribe. Duerme esperando al teclado y mira
  el brillo cada segundo y la batería cada 20 s.

Ver lo que hace: `journalctl -u zeropowerd -f`.

## Medidas

Las del autor de cz-battery-saver en un Cardputer Zero (pantalla al 30 %, wifi conectado),
que son las que motivaron cada ajuste:

| Cambio | Consumo |
|---|---|
| Tal cual | ~416 mA |
| Ethernet USB en auto-suspend | ~311 mA |
| + audio del lanzador aparcado | ~277 mA |
| Pantalla apagada + lanzador en pausa (con lo anterior) | ~233 mA |

En el Cardputer de este repositorio, tal cual y en reposo: **378 mA** (wifi, brillo al 100 %).
Las aplicaciones hechas con la plantilla oficial además tenían un núcleo de la CPU al 100 %
mientras estaban abiertas (arreglado en esta y en las demás apps de este autor).

## Instalar

En el aparato, desde la tienda del Cardputer Zero, o con el `.deb`:

```sh
sudo apt install ./ZeroPower_<versión>_rev1_arm64.deb
```

Desinstalar (devuelve todos los ajustes): `sudo apt remove zeropower`.

## Compilar

```sh
Tools/compilar.sh pruebas     # simulador de escritorio (SDL2) y pruebas
Tools/compilar.sh deb         # dist/ZeroPower_<versión>_rev1_arm64.deb para el aparato
Tools/compilar.sh simulador   # abre el simulador en este escritorio (X11)
```

Usa Docker si lo hay y, si no, las herramientas de la máquina (las de `Tools/Dockerfile`). En
el simulador no está el ayudante: la app simula el sistema en memoria.

## Créditos y licencia

- Hecha sobre la [plantilla oficial](https://github.com/CardputerZero/Template) del Cardputer
  Zero (MIT, © M5Stack CardputerZero Community), con LVGL (MIT) e iconos Phosphor (MIT; ver
  `assets/fonts/LICENSE.txt`).
- Las ideas del ahorro con la pantalla apagada, el audio del lanzador y el Ethernet en
  auto-suspend, y sus medidas, son de
  [cz-battery-saver](https://github.com/InstantComet/cz-battery-saver) de InstantComet (MIT).
  El código de ZeroPower es propio.
- El código es MIT (ver `LICENSE`).

## Estado

**0.1.4** (24-nov-2026): la pantalla *Battery* dice en qué posición está el interruptor USB y lo que ahorra *Slave* (medido: 88 mA). **0.1.3** (22-nov-2026): *Screen timeout*; las apps abiertas ya no se confunden con un `nmcli` terminado del lanzador; el registro dice por qué despierta. **0.1.2** (20-nov-2026): sin fichero de sudoers y `zeropowerd` como servicio del sistema con el usuario del aparato (lo exige la tienda). **0.1.1**: los ajustes de sí/no son interruptores, como en iOS. **0.1.0**: primera versión. Probada en el simulador; en el aparato, el ayudante
lee bien el estado del sistema. Falta probar en el aparato cada ajuste y el servicio.
