# CS2 Open World

CS2 Open World — серверный плагин для Counter-Strike 2 на C++, Metamod:Source
и hl2sdk-cs2. Ядро плагина находится в `src/core/`; игровые и инфраструктурные
компоненты расположены в `src/adapters/`, `src/modules/` и `src/ui/`.

Плагин авторитетен на стороне сервера. Steam-аутентификация должна быть
подтверждена движком; одного ненулевого SteamID недостаточно.

## Документация

- [Настройка и развёртывание](./SETUP.ru.md) ([English](./SETUP.md))
- [Руководство по созданию карт](./MAP_AUTHORING.ru.md)
  ([English](./MAP_AUTHORING.md))
- [Полная архитектурная спецификация](./CS2%20Open%20World.ru.md)
  ([English overview](./ARCHITECTURE.md))
- [План развития](./docs/ROADMAP.md)

## Быстрый запуск

Linux:

```bash
scripts/install.sh --check
scripts/install.sh
```

Windows:

```powershell
.\scripts\install.ps1 -Check
.\scripts\install.ps1
```

Установщики проверяют зависимости и спрашивают подтверждение перед
изменениями. Они могут инициализировать подмодули SDK, создать `.env`,
подготовить PostgreSQL, собрать плагин и запустить тесты.

## Лицензия

Оригинальный код проекта распространяется по GNU Affero General Public
License версии 3 (AGPL-3.0-only); см. [LICENSE](./LICENSE). Лицензии сторонних
компонентов перечислены в [THIRD_PARTY_NOTICES.md](./THIRD_PARTY_NOTICES.md).
AGPL сама по себе не запрещает коммерческое использование или продажу и не
отменяет отдельные правила распространения Valve.
