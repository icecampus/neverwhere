# PGG

Язык процедурной генерации геометрии **вынесен** из Neverwhere в отдельный репозиторий: <https://github.com/rezzeted/pgg>

В этом репозитории его нет: ни `src/libs/pgg`, ни `PggTool`/`PggViewer`, ни `resources/pgg`, ни MCP `neverwhere-pgg`. Сборка Neverwhere не тянет ANTLR. Здания в редакторе — ассеты `building3d` (GLB на клетке), не модели `.pgg`.

PGG не подключать submodule’ом, пока нет явного решения по интеграции в редактор/клиент. Спека, CLI, вьюер, тесты и арт-примеры живут только в репозитории выше.
