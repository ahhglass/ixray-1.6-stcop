# Звуки снаряжения и брони
> [!IMPORTANT]
> **Статус**: Поддерживается <br>
> **Минимальная версия**: 1.6

## Обзор

Система звуков снаряжения позволяет настраивать звуки шагов, прыжков и пробития для различных типов брони и шлемов. Все звуки загружаются автоматически при надевании снаряжения.

## Параметры для брони (CCustomOutfit)

### Звуки шагов и движения

```ini
[outfit_section]
step_sound_clank          = material\step\equipment\clank\light_armor\clank_    ; звуки стука при ходьбе
step_sound_rustle         = material\step\equipment\rustle\light_armor\rustle_ ; звуки шуршания при ходьбе
sound_jump_equipment      = material\step\equipment\push\light_armor\push_       ; звуки при прыжке
```

### Звуки пробития

```ini
[outfit_section]
deflection_sound_path     = wepl\hit_effect\body\hit_light_  ; путь к звукам пробития брони
armor_type                = light                            ; тип брони (light, medium, heavy, exo)
```

### Типы брони

- `light` - лёгкая броня
- `medium` - средняя броня
- `heavy` - тяжёлая броня
- `exo` - экзоскелет (также определяется автоматически по `is_exo` или `is_exo_proto`)

## Параметры для шлемов (CHelmet)

```ini
[helmet_section]
deflection_sound_path     = wepl\hit_effect\head\hit_helmet_  ; путь к звукам пробития шлема
helmet_type               = helmet                            ; тип шлема (glass, gasmask, helmet, exo)
```

### Типы шлемов

- `glass` - шлем со стеклом (также определяется автоматически по `glass_present`)
- `gasmask` - противогаз (также определяется автоматически по `hud_gas_mask_avaliable`)
- `helmet` - обычный шлем
- `exo` - экзоскелетный шлем

## Параметры для актора (по умолчанию)

Звуки, которые используются когда на акторе нет брони:

```ini
[actor]
step_sound_rustle         = material\step\equipment\rustle\light_armor\rustle_  ; звуки шуршания по умолчанию
sound_jump_equipment      = material\step\equipment\push\light_armor\push_      ; звуки прыжка по умолчанию
```

## Формат путей к звукам

### Важно: использование маски поиска

Пути к звукам должны заканчиваться на подчеркивание `_` или содержать символ `*` для автоматического поиска всех файлов с этим префиксом:

```ini
; Правильно - система автоматически добавит * в конец
step_sound_clank = material\step\equipment\clank\light_armor\clank_

; Правильно - маска указана явно
step_sound_clank = material\step\equipment\clank\light_armor\clank_*

; Неправильно - без подчеркивания или звездочки
step_sound_clank = material\step\equipment\clank\light_armor\clank
```

### Структура файлов

Система автоматически находит все файлы, соответствующие маске. Например, для пути `material\step\equipment\clank\light_armor\clank_*` будут загружены:
- `clank_1.ogg`
- `clank_2.ogg`
- `clank_3.ogg`
- и т.д.

### Примеры путей

**Звуки шагов:**
```
material\step\equipment\clank\light_armor\clank_
material\step\equipment\clank\average_armor\clank_
material\step\equipment\clank\heavy_armor\clank_
material\step\equipment\clank\special_armor\clank_
```

**Звуки шуршания:**
```
material\step\equipment\rustle\light_armor\rustle_
material\step\equipment\rustle\average_armor\rustle_
material\step\equipment\rustle\heavy_armor\rustle_
```

**Звуки прыжка:**
```
material\step\equipment\push\light_armor\push_
material\step\equipment\push\average_armor\push_
material\step\equipment\push\heavy_armor\push_
material\step\equipment\push\exoskeleton\push_
```

**Звуки пробития (броня):**
```
wepl\hit_effect\body\hit_light_
wepl\hit_effect\body\hit_medium_
wepl\hit_effect\body\hit_heavy_
wepl\hit_effect\body\hit_exo_
wepl\hit_effect\body\hit_none_
```

**Звуки пробития (шлем):**
```
wepl\hit_effect\head\hit_helmet_
wepl\hit_effect\head\hit_glass_
wepl\hit_effect\head\hit_gasmask_
wepl\hit_effect\head\hit_exo_
wepl\hit_effect\head\hit_none_
```

## Полный пример конфигурации

```ini
;====| ЛЁГКОЕ СНАРЯЖЕНИЕ |====
[novice_outfit]
step_sound_clank          = material\step\equipment\clank\light_armor\clank_
step_sound_rustle         = material\step\equipment\rustle\light_armor\rustle_
sound_jump_equipment      = material\step\equipment\push\light_armor\push_
deflection_sound_path     = wepl\hit_effect\body\hit_light_
armor_type                = light

;====| СРЕДНЕЕ СНАРЯЖЕНИЕ |====
[stalker_outfit]
step_sound_clank          = material\step\equipment\clank\average_armor\clank_
step_sound_rustle         = material\step\equipment\rustle\average_armor\rustle_
sound_jump_equipment      = material\step\equipment\push\average_armor\push_
deflection_sound_path     = wepl\hit_effect\body\hit_medium_
armor_type                = medium

;====| ТЯЖЁЛОЕ СНАРЯЖЕНИЕ |====
[military_outfit]
step_sound_clank          = material\step\equipment\clank\heavy_armor\clank_
step_sound_rustle         = material\step\equipment\rustle\heavy_armor\rustle_
sound_jump_equipment      = material\step\equipment\push\heavy_armor\push_
deflection_sound_path     = wepl\hit_effect\body\hit_heavy_
armor_type                = heavy

;====| ЭКЗОСКЕЛЕТ |====
[exo_outfit]
is_exo                    = true
step_sound_clank          = material\step\equipment\clank\heavy_armor\clank_
step_sound_rustle         = material\step\equipment\rustle\heavy_armor\rustle_
sound_jump_equipment      = material\step\equipment\push\exoskeleton\push_
deflection_sound_path     = wepl\hit_effect\body\hit_exo_
armor_type                = exo

;====| ШЛЕМЫ |====
[helm_hardhat]
deflection_sound_path     = wepl\hit_effect\head\hit_helmet_
helmet_type               = helmet

[helm_protective]
deflection_sound_path     = wepl\hit_effect\head\hit_glass_
helmet_type               = glass
glass_present             = true

[helm_respirator]
deflection_sound_path      = wepl\hit_effect\head\hit_gasmask_
helmet_type               = gasmask
hud_gas_mask_avaliable    = true
```

## Примечания

1. **Автоматическое определение типа**: Тип брони/шлема может определяться автоматически по свойствам (`is_exo`, `glass_present`, `hud_gas_mask_avaliable`), но рекомендуется явно указывать `armor_type` и `helmet_type` для надёжности.

2. **Пути относительно `$game_sounds$`**: Все пути указываются относительно корневой папки звуков игры. Не нужно указывать полный путь.

3. **Расширение файлов**: Система автоматически ищет файлы с расширением `.ogg`. Расширение указывать не нужно.

4. **Производительность**: Звуки загружаются при надевании снаряжения и выгружаются при снятии. Кэширование предотвращает повторную загрузку одинаковых звуков.

5. **Обратная совместимость**: Если параметр не указан, система использует значения по умолчанию или не воспроизводит звук.

