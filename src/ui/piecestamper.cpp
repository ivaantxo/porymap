#include "piecestamper.h"
#include "config.h"
#include "editcommands.h"
#include "editor.h"
#include "filedialog.h"
#include "layoutpixmapitem.h"
#include "message.h"
#include "project.h"
#include "stamping.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>

namespace {

int floorDiv(int a, int b) {
    return (a >= 0) ? a / b : -((-a + b - 1) / b);
}

QString pieceSizeText(const QSize &size) {
    return QString("%1×%2 px").arg(size.width()).arg(size.height());
}

} // namespace

void PieceSheetItem::setImage(const QImage &image) {
    QPixmap pixmap(image.width() * zoom, image.height() * zoom);
    QPainter painter(&pixmap);
    // Checkerboard behind the transparent parts, one square per 4x4 pixels.
    const int square = 4 * zoom;
    for (int y = 0; y < pixmap.height(); y += square)
    for (int x = 0; x < pixmap.width(); x += square)
        painter.fillRect(x, y, square, square, ((x + y) / square) % 2 ? QColor(204, 204, 204) : QColor(255, 255, 255));
    painter.drawImage(pixmap.rect(), image);
    painter.end();

    this->basePixmap = pixmap;
    setPixmap(pixmap);
    select(QPoint(0, 0), QSize(cellsWide(), cellsTall()));
}

QRect PieceSheetItem::selectedRect() const {
    auto *self = const_cast<PieceSheetItem*>(this);
    QPoint start = self->getSelectionStart();
    QSize size = getSelectionDimensions();
    return QRect(start.x() * Tile::pixelWidth(), start.y() * Tile::pixelHeight(),
                 size.width() * Tile::pixelWidth(), size.height() * Tile::pixelHeight());
}

void PieceSheetItem::draw() {
    setPixmap(this->basePixmap);
    drawSelection();
}

PieceStamper::PieceStamper(Editor *editor, QWidget *parent) : QWidget(parent), editor(editor) {
    auto *layout = new QVBoxLayout(this);

    // Library
    auto *libraryRow = new QHBoxLayout();
    this->comboBox_Pieces = new QComboBox(this);
    this->comboBox_Pieces->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    this->comboBox_Pieces->setMinimumContentsLength(8);
    this->button_Import = new QPushButton("Importar…", this);
    this->button_Import->setToolTip("Añadir imágenes a la biblioteca de piezas. Sirve cualquier PNG cuyo ancho y alto sean múltiplos de 8, con los colores que sean.");
    this->button_Remove = new QPushButton("Quitar", this);
    this->button_Remove->setToolTip("Quitar la imagen de la biblioteca (no la borra del disco).");
    libraryRow->addWidget(this->comboBox_Pieces, 1);
    libraryRow->addWidget(this->button_Import);
    libraryRow->addWidget(this->button_Remove);
    layout->addLayout(libraryRow);

    this->sheetScene = new QGraphicsScene(this);
    this->sheetItem = new PieceSheetItem();
    this->sheetScene->addItem(this->sheetItem);
    this->sheetView = new QGraphicsView(this->sheetScene, this);
    this->sheetView->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    this->sheetView->setMinimumHeight(120);
    this->sheetView->setToolTip("Arrastra para elegir qué trozo de la imagen se estampa.");
    layout->addWidget(this->sheetView, 1);

    this->label_Piece = new QLabel(this);
    this->label_Piece->setWordWrap(true);
    layout->addWidget(this->label_Piece);

    // Where and how to stamp
    auto *layerBox = new QGroupBox("Capa donde se estampa", this);
    auto *layerRow = new QHBoxLayout(layerBox);
    this->layerGroup = new QButtonGroup(this);
    const QStringList layerNames = {"Baja", "Media", "Alta"};
    const QStringList layerTips = {
        "Debajo de todo: el suelo.",
        "Encima del suelo y debajo de los personajes.",
        "Encima de los personajes: copas de árbol, tejados.",
    };
    for (int i = 0; i < layerNames.length(); i++) {
        auto *button = new QRadioButton(layerNames.at(i), layerBox);
        button->setToolTip(layerTips.at(i));
        this->layerGroup->addButton(button, i);
        layerRow->addWidget(button);
    }
    this->layerGroup->button(mapeado::CAPA_MEDIA)->setChecked(true);
    layout->addWidget(layerBox);

    auto *optionsRow = new QHBoxLayout();
    this->checkBox_Replace = new QCheckBox("Lo transparente borra", this);
    this->checkBox_Replace->setToolTip("Sin marcar, lo transparente de la pieza deja ver lo que ya había en la capa. Marcado, lo borra.");
    this->comboBox_Grid = new QComboBox(this);
    this->comboBox_Grid->addItem("Rejilla de 8 px", 8);
    this->comboBox_Grid->addItem("Rejilla de 16 px", 16);
    this->comboBox_Grid->setToolTip("Dónde puede caer la esquina de la pieza.");
    optionsRow->addWidget(this->checkBox_Replace);
    optionsRow->addStretch(1);
    optionsRow->addWidget(this->comboBox_Grid);
    layout->addLayout(optionsRow);

    auto *showBox = new QGroupBox("Capas que se ven", this);
    auto *showRow = new QHBoxLayout(showBox);
    for (const QString &name : layerNames) {
        auto *checkBox = new QCheckBox(name, showBox);
        checkBox->setChecked(true);
        this->checkBoxes_ShowLayer.append(checkBox);
        showRow->addWidget(checkBox);
        connect(checkBox, &QCheckBox::toggled, this, &PieceStamper::applyLayerVisibility);
    }
    showBox->setToolTip("Solo mientras esta pestaña está abierta.");
    layout->addWidget(showBox);

    // What's left in the tileset
    auto *usageRow = new QHBoxLayout();
    this->label_Usage = new QLabel(this);
    this->label_Usage->setTextFormat(Qt::RichText);
    this->button_Optimize = new QPushButton("Optimizar tileset", this);
    this->button_Optimize->setToolTip("Reempaqueta el tileset desde lo pintado en todos sus mapas: junta metatiles repetidos, quita los que no usa nadie y libera los tiles y colores que sobran.");
    usageRow->addWidget(this->label_Usage, 1);
    usageRow->addWidget(this->button_Optimize, 0, Qt::AlignBottom);
    layout->addLayout(usageRow);

    this->label_Status = new QLabel(this);
    this->label_Status->setWordWrap(true);
    layout->addWidget(this->label_Status);

    connect(this->button_Import, &QPushButton::clicked, this, &PieceStamper::importPieces);
    connect(this->button_Remove, &QPushButton::clicked, this, &PieceStamper::removePiece);
    connect(this->comboBox_Pieces, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PieceStamper::loadPiece);
    connect(this->sheetItem, &SelectablePixmapItem::selectionChanged, this, &PieceStamper::updatePiece);
    connect(this->button_Optimize, &QPushButton::clicked, this, &PieceStamper::optimize);

    setStatus("Con el lápiz, haz clic en el mapa para estampar la pieza. Arrastrando se repite.");
}

void PieceStamper::setTabOpen(bool open) {
    if (this->tabOpen == open)
        return;
    this->tabOpen = open;
    if (!open)
        hidePreview();
    applyLayerVisibility();
}

bool PieceStamper::isActive() const {
    return this->tabOpen
        && this->unsupportedReason.isEmpty()
        && !this->pieceImage.isNull()
        && this->editor->layout
        && this->editor->layout->tileset_primary;
}

void PieceStamper::refresh() {
    this->unsupportedReason = Stamping::unsupportedReason();
    if (this->pieceListDir != userConfig.projectDir())
        loadPieceList();
    if (!this->unsupportedReason.isEmpty())
        setStatus(this->unsupportedReason);
    hidePreview();
    updateUsage();
}

void PieceStamper::loadPieceList() {
    this->pieceListDir = userConfig.projectDir();
    const QSignalBlocker blocker(this->comboBox_Pieces);
    this->comboBox_Pieces->clear();
    const QDir root(userConfig.projectDir());
    for (const QString &path : userConfig.stampPieces) {
        this->comboBox_Pieces->addItem(QFileInfo(path).fileName(), path);
        this->comboBox_Pieces->setItemData(this->comboBox_Pieces->count() - 1, root.filePath(path), Qt::ToolTipRole);
    }
    loadPiece(this->comboBox_Pieces->currentIndex());
}

void PieceStamper::savePieceList() {
    QStringList paths;
    for (int i = 0; i < this->comboBox_Pieces->count(); i++)
        paths.append(this->comboBox_Pieces->itemData(i).toString());
    userConfig.stampPieces = paths;
    userConfig.save();
}

void PieceStamper::importPieces() {
    const QDir root(userConfig.projectDir());
    QStringList files = FileDialog::getOpenFileNames(this, "Importar piezas", root.path(), "Imágenes (*.png *.bmp *.gif)");
    if (files.isEmpty())
        return;

    QStringList rejected;
    int lastAdded = -1;
    for (const QString &file : files) {
        QImage image(file);
        if (image.isNull() || image.width() % Tile::pixelWidth() || image.height() % Tile::pixelHeight()) {
            rejected.append(QString("%1 (%2)").arg(QFileInfo(file).fileName())
                                              .arg(image.isNull() ? QString("no se puede leer") : pieceSizeText(image.size())));
            continue;
        }
        QString path = root.relativeFilePath(file);
        if (path.startsWith(".."))
            path = QFileInfo(file).absoluteFilePath();
        int index = this->comboBox_Pieces->findData(path);
        if (index < 0) {
            const QSignalBlocker blocker(this->comboBox_Pieces);
            this->comboBox_Pieces->addItem(QFileInfo(path).fileName(), path);
            index = this->comboBox_Pieces->count() - 1;
            this->comboBox_Pieces->setItemData(index, root.filePath(path), Qt::ToolTipRole);
        }
        lastAdded = index;
    }
    savePieceList();
    if (lastAdded >= 0) {
        if (this->comboBox_Pieces->currentIndex() == lastAdded)
            loadPiece(lastAdded);
        else
            this->comboBox_Pieces->setCurrentIndex(lastAdded);
    }
    if (!rejected.isEmpty()) {
        WarningMessage::show("Algunas imágenes no se han importado.",
                             "El ancho y el alto tienen que ser múltiplos de 8:\n" + rejected.join("\n"), this);
    }
}

void PieceStamper::removePiece() {
    int index = this->comboBox_Pieces->currentIndex();
    if (index < 0)
        return;
    this->comboBox_Pieces->removeItem(index);
    savePieceList();
    if (this->comboBox_Pieces->count() == 0)
        loadPiece(-1);
}

void PieceStamper::loadPiece(int index) {
    this->sheetImage = QImage();
    if (index >= 0) {
        QString path = this->comboBox_Pieces->itemData(index, Qt::ToolTipRole).toString();
        QImage image(path);
        if (image.isNull()) {
            setStatus(QString("No se puede leer %1.").arg(path));
        } else if (image.width() % Tile::pixelWidth() || image.height() % Tile::pixelHeight()) {
            setStatus(QString("%1 mide %2: el ancho y el alto tienen que ser múltiplos de 8.").arg(path).arg(pieceSizeText(image.size())));
        } else {
            this->sheetImage = image.convertToFormat(QImage::Format_ARGB32);
        }
    }

    if (this->sheetImage.isNull()) {
        this->sheetItem->setPixmap(QPixmap());
        this->sheetScene->setSceneRect(QRectF());
        this->pieceImage = QImage();
        this->piece = mapeado::Imagen();
        this->label_Piece->setText("Sin pieza: importa una imagen.");
        hidePreview();
        return;
    }
    this->sheetItem->setImage(this->sheetImage); // Selects all of it, which updates the piece
    this->sheetScene->setSceneRect(this->sheetItem->boundingRect());
}

void PieceStamper::updatePiece() {
    if (this->sheetImage.isNull())
        return;
    QRect rect = this->sheetItem->selectedRect().intersected(this->sheetImage.rect());
    this->pieceImage = this->sheetImage.copy(rect);
    this->piece = Stamping::pieceFromImage(this->pieceImage);
    QString text = QString("Pieza: %1 desde (%2, %3)").arg(pieceSizeText(rect.size())).arg(rect.x()).arg(rect.y());

    const int w = Metatile::pixelWidth(), h = Metatile::pixelHeight();
    if (this->pieceImage.size() == QSize(3 * w, 3 * h)) {
        for (int k = 0; k < 9; k++)
            this->smartPathPieces[k] = Stamping::pieceFromImage(this->pieceImage.copy((k % 3) * w, (k / 3) * h, w, h));
        QImage open = this->pieceImage.copy(w, h, w, h);
        this->smartPathPreview = QImage(2 * w, 2 * h, QImage::Format_ARGB32);
        this->smartPathPreview.fill(Qt::transparent);
        QPainter painter(&this->smartPathPreview);
        for (int i = 0; i < 4; i++)
            painter.drawImage((i % 2) * w, (i / 2) * h, open);
        painter.end();
        text += " · también camino inteligente (Smart Paths o Mayús)";
    }
    this->label_Piece->setText(text);
    if (this->preview)
        this->preview->setPixmap(QPixmap::fromImage(this->previewIsSmartPath ? this->smartPathPreview : this->pieceImage));
}

QPoint PieceStamper::snapToGrid(const QPointF &pos) const {
    int grid = this->comboBox_Grid->currentData().toInt();
    return QPoint(floorDiv(qFloor(pos.x()), grid) * grid, floorDiv(qFloor(pos.y()), grid) * grid);
}

// While dragging, the piece repeats edge to edge from where the stroke started.
QPoint PieceStamper::strokePos(const QPointF &pos) const {
    int w = this->pieceImage.width(), h = this->pieceImage.height();
    int dx = floorDiv(qFloor(pos.x()) - this->strokeOrigin.x(), w);
    int dy = floorDiv(qFloor(pos.y()) - this->strokeOrigin.y(), h);
    return this->strokeOrigin + QPoint(dx * w, dy * h);
}

bool PieceStamper::isSmartPathMode(Qt::KeyboardModifiers modifiers) const {
    // Same rule as porymap's smart paths: the toolbar checkbox, and Shift flips it.
    if (this->pieceImage.size() != QSize(3 * Metatile::pixelWidth(), 3 * Metatile::pixelHeight()))
        return false;
    bool shift = modifiers & Qt::ShiftModifier;
    return this->editor->settings->smartPathsEnabled != shift;
}

void PieceStamper::mapMouseEvent(QGraphicsSceneMouseEvent *event, LayoutPixmapItem *item) {
    if (event->type() == QEvent::GraphicsSceneMousePress) {
        if (!(event->buttons() & Qt::LeftButton))
            return;
        this->actionId++;
        this->stroking = true;
        this->strokeFailed = false;
        this->strokeSmartPath = isSmartPathMode(event->modifiers());
        if (this->strokeSmartPath) {
            QPoint cell = Metatile::coordFromPixmapCoord(event->pos());
            updatePreview(QPoint(cell.x() * Metatile::pixelWidth(), cell.y() * Metatile::pixelHeight()), true);
            stampSmartPathAt(cell);
        } else {
            this->strokeOrigin = snapToGrid(event->pos());
            // Report moves inside a metatile too, the grid is finer than that.
            item->trackPixelMoves = true;
            updatePreview(this->strokeOrigin, false);
            stampAt(this->strokeOrigin);
        }
    } else if (event->type() == QEvent::GraphicsSceneMouseMove) {
        if (!this->stroking || this->strokeFailed)
            return;
        if (this->strokeSmartPath) {
            QPoint cell = Metatile::coordFromPixmapCoord(event->pos());
            updatePreview(QPoint(cell.x() * Metatile::pixelWidth(), cell.y() * Metatile::pixelHeight()), true);
            if (cell != this->lastStampPos)
                stampSmartPathAt(cell);
        } else {
            QPoint pos = strokePos(event->pos());
            updatePreview(pos, false);
            if (pos != this->lastStampPos)
                stampAt(pos);
        }
    } else if (event->type() == QEvent::GraphicsSceneMouseRelease) {
        this->stroking = false;
        item->trackPixelMoves = false;
    }
}

// One stamping operation: the tileset and its maps as the library sees them, changed by
// one or more stamps, and applied to porymap only if all of them fit.
struct PieceStamper::StampJob {
    Layout *layout = nullptr;
    Tileset *tileset = nullptr;
    mapeado::Formato format;
    mapeado::Tileset data;
    Stamping::TilesetMaps maps;
    int target = -1;
    std::vector<int> fixedTiles;
    mapeado::Estampado total;
};

bool PieceStamper::beginStamp(StampJob *job) {
    job->layout = this->editor->layout;
    job->tileset = job->layout ? job->layout->tileset_primary : nullptr;
    if (!job->tileset)
        return false;

    if (this->canEditTileset && !this->canEditTileset()) {
        this->strokeFailed = true;
        return false;
    }

    QString error;
    if (!Stamping::collectMaps(this->editor->project, job->tileset, &job->maps, &error)) {
        this->strokeFailed = true;
        setStatus(error);
        return false;
    }
    job->target = job->maps.indexOf(job->layout);
    if (job->target < 0)
        return false;

    job->format = Stamping::format();
    job->data = Stamping::fromTileset(job->tileset);
    job->fixedTiles = Stamping::fixedTiles(job->tileset);
    return true;
}

bool PieceStamper::addStamp(StampJob &job, const mapeado::Imagen &piece, const QPoint &pos, bool replace) {
    if (job.total.resultado != mapeado::ESTAMPADO)
        return false;
    auto layer = static_cast<mapeado::Capa>(this->layerGroup->checkedId());
    mapeado::Estampado result = mapeado::Estampar(job.format, job.data, job.maps.maps, job.target, piece,
                                                  pos.x(), pos.y(), layer, replace, job.fixedTiles);
    if (result.resultado != mapeado::ESTAMPADO) {
        job.total.resultado = result.resultado;
        job.total.mensaje = result.mensaje;
        return false;
    }
    job.total.metatilesNuevos += result.metatilesNuevos;
    job.total.tilesNuevos += result.tilesNuevos;
    job.total.coloresNuevos += result.coloresNuevos;
    return true;
}

bool PieceStamper::finishStamp(StampJob &job, const std::function<void()> &retry) {
    const mapeado::Estampado result = job.total;
    if (result.resultado != mapeado::ESTAMPADO) {
        this->strokeFailed = true;
        setStatus("No cabe: " + QString::fromStdString(result.mensaje) + ".");
        // After the mouse event is done, so the dialog doesn't swallow the release.
        QTimer::singleShot(0, this, [this, result, retry] { showStampFailure(result, retry); });
        return false;
    }

    Layout *layout = job.layout;
    Blockdata oldBlocks = layout->blockdata;
    Blockdata newBlocks = Stamping::toBlockdata(job.maps.maps[job.target].bloques);
    int changed = 0;
    for (int i = 0; i < oldBlocks.size() && i < newBlocks.size(); i++) {
        if (oldBlocks.at(i) != newBlocks.at(i))
            changed++;
    }
    if (changed == 0)
        return true;

    bool tilesetChanged = Stamping::applyToTileset(job.data, job.tileset);
    layout->editHistory.push(new StampPiece(layout, oldBlocks, newBlocks, this->actionId));
    if (tilesetChanged) {
        if (this->editor->metatile_selector_item)
            this->editor->metatile_selector_item->refresh();
        if (this->tilesetEdited)
            this->tilesetEdited();
    }
    updateUsage();
    setStatus(QString("Estampada en %1 casilla%2: %3 metatiles, %4 tiles y %5 colores nuevos.")
              .arg(changed).arg(changed == 1 ? "" : "s")
              .arg(result.metatilesNuevos).arg(result.tilesNuevos).arg(result.coloresNuevos));
    return true;
}

bool PieceStamper::stampAt(const QPoint &pos) {
    this->lastStampPos = pos;
    Layout *layout = this->editor->layout;
    if (!layout || this->pieceImage.isNull())
        return false;

    // Nothing to do if it falls completely outside the map.
    if (!QRect(pos, this->pieceImage.size()).intersects(QRect(QPoint(0, 0), layout->pixelSize())))
        return false;

    StampJob job;
    if (!beginStamp(&job))
        return false;
    addStamp(job, this->piece, pos, this->checkBox_Replace->isChecked());
    return finishStamp(job, [this, pos] { stampAt(pos); });
}

// A cell is part of the smart path if its art on the stamping layer is one of the 3x3
// pieces. Porymap's own smart paths go by metatile numbers instead; going by the art,
// what's under the path doesn't matter, and optimizing the tileset can't break it.
bool PieceStamper::isSmartPathCell(const StampJob &job, const QPoint &cell) const {
    const mapeado::MapaDelTileset &map = job.maps.maps[job.target];
    if (cell.x() < 0 || cell.y() < 0 || cell.x() >= map.ancho || cell.y() >= map.alto)
        return false;
    int metatile = map.bloques[cell.y() * map.ancho + cell.x()] & job.format.mascaraId;
    if (metatile >= static_cast<int>(job.data.metatiles.size()))
        return false;

    mapeado::Imagen layers[mapeado::NUM_CAPAS];
    for (auto &layer : layers)
        layer = mapeado::Imagen(Metatile::pixelWidth(), Metatile::pixelHeight());
    mapeado::PintarMetatile(job.data, metatile, layers, 0, 0);
    const std::vector<mapeado::Color> &art = layers[this->layerGroup->checkedId()].px;
    for (const mapeado::Imagen &piece : this->smartPathPieces) {
        if (piece.px == art)
            return true;
    }
    return false;
}

// Like porymap's smart paths (LayoutPixmapItem::paintSmartPath): a 2x2 block of the open
// piece, and then the cells around it that are part of the path get the piece that
// matches their neighbors. The pieces replace whatever was on that layer.
bool PieceStamper::stampSmartPathAt(const QPoint &cell) {
    this->lastStampPos = cell;
    Layout *layout = this->editor->layout;
    if (!layout || this->pieceImage.isNull())
        return false;
    if (!layout->isWithinBounds(cell) && !layout->isWithinBounds(cell + QPoint(1, 1)))
        return false;

    StampJob job;
    if (!beginStamp(&job))
        return false;

    auto pixelPos = [](const QPoint &c) { return QPoint(c.x() * Metatile::pixelWidth(), c.y() * Metatile::pixelHeight()); };
    for (int j = 0; j <= 1; j++)
    for (int i = 0; i <= 1; i++) {
        QPoint c = cell + QPoint(i, j);
        if (layout->isWithinBounds(c))
            addStamp(job, this->smartPathPieces[4], pixelPos(c), true);
    }
    for (int j = -1; j <= 2; j++)
    for (int i = -1; i <= 2; i++) {
        // The corners can't be affected.
        if ((i == -1 || i == 2) && (j == -1 || j == 2))
            continue;
        QPoint c = cell + QPoint(i, j);
        if (job.total.resultado != mapeado::ESTAMPADO || !isSmartPathCell(job, c))
            continue;
        int neighbors = 0;
        if (isSmartPathCell(job, c + QPoint(0, -1))) neighbors += 1;
        if (isSmartPathCell(job, c + QPoint(1, 0)))  neighbors += 2;
        if (isSmartPathCell(job, c + QPoint(0, 1)))  neighbors += 4;
        if (isSmartPathCell(job, c + QPoint(-1, 0))) neighbors += 8;
        addStamp(job, this->smartPathPieces[LayoutPixmapItem::smartPathTileIndex(neighbors)], pixelPos(c), true);
    }
    return finishStamp(job, [this, cell] { stampSmartPathAt(cell); });
}

void PieceStamper::showStampFailure(const mapeado::Estampado &result, const std::function<void()> &retry) {
    bool optimizable = result.resultado == mapeado::SIN_HUECO_TILES
                    || result.resultado == mapeado::SIN_HUECO_PALETAS
                    || result.resultado == mapeado::SIN_HUECO_METATILES;

    QString text = QString::fromStdString(result.mensaje);
    if (!text.isEmpty())
        text[0] = text[0].toUpper();
    Message msgBox(QMessageBox::Warning, "No se puede estampar la pieza.", QMessageBox::NoButton, this);
    msgBox.setInformativeText(text + "." + (optimizable ? "\n\nOptimizar el tileset puede dejar sitio: quita lo que no usa ningún mapa." : ""));
    QPushButton *optimizeButton = optimizable ? msgBox.addButton("Optimizar y reintentar", QMessageBox::AcceptRole) : nullptr;
    msgBox.setDefaultButton(msgBox.addButton(optimizable ? "Cancelar" : "Vale", QMessageBox::RejectRole));
    msgBox.exec();
    if (optimizeButton && msgBox.clickedButton() == optimizeButton && optimize()) {
        this->actionId++;
        retry();
    }
}

bool PieceStamper::optimize() {
    Layout *layout = this->editor->layout;
    Tileset *tileset = layout ? layout->tileset_primary : nullptr;
    if (!tileset || !this->unsupportedReason.isEmpty())
        return false;
    if (this->canEditTileset && !this->canEditTileset())
        return false;

    Stamping::TilesetMaps maps;
    QString error;
    if (!Stamping::collectMaps(this->editor->project, tileset, &maps, &error)) {
        ErrorMessage::show("No se puede optimizar el tileset.", error, this);
        return false;
    }
    QStringList names;
    for (const Layout *mapLayout : maps.layouts) {
        names.append(mapLayout->name);
        if (mapLayout->getBorderWidth() != 2 || mapLayout->getBorderHeight() != 2) {
            ErrorMessage::show("No se puede optimizar el tileset.",
                               QString("El borde de %1 no es de 2×2 metatiles, y optimizar solo sabe de bordes de 2×2.").arg(mapLayout->name), this);
            return false;
        }
    }

    Message question(QMessageBox::Question, QString("¿Optimizar %1?").arg(tileset->name), QMessageBox::NoButton, this);
    question.setInformativeText(QString("Se reempaqueta desde lo pintado en los mapas que lo usan (%1): se juntan los metatiles "
                                        "repetidos, se quitan los que no usa nadie y se liberan los tiles y colores que sobran. "
                                        "Los metatiles con nombre y los de los prefabs se quedan siempre en su sitio.\n\n"
                                        "Se guardan el tileset y esos mapas, con lo que tuvieran sin guardar, "
                                        "y su historial de deshacer se vacía.")
                                .arg(names.join(", ")));
    QPushButton *optimizeButton = question.addButton("Optimizar", QMessageBox::AcceptRole);
    question.addButton("Cancelar", QMessageBox::RejectRole);
    question.setDefaultButton(optimizeButton);
    question.exec();
    if (question.clickedButton() != optimizeButton)
        return false;

    const Stamping::Usage before = Stamping::usage(tileset);

    mapeado::Salida out;
    std::string optimizeError;
    if (!mapeado::Optimizar(Stamping::format(), Stamping::fromTileset(tileset), maps.maps,
                            Stamping::pinnedMetatiles(tileset), Stamping::fixedTiles(tileset), false, out, optimizeError)) {
        ErrorMessage::show("No se puede optimizar el tileset.", QString::fromStdString(optimizeError), this);
        return false;
    }

    Stamping::applyToTileset(out.tileset, tileset);
    bool saved = true;
    for (int i = 0; i < maps.layouts.length(); i++) {
        Layout *mapLayout = maps.layouts.at(i);
        mapLayout->setBlockdata(Stamping::toBlockdata(out.bloques.at(i)));
        mapLayout->setBorderBlockData(Stamping::toBlockdata(out.bloquesBorde.at(i)));
        mapLayout->lastCommitBlocks.blocks = mapLayout->blockdata;
        mapLayout->lastCommitBlocks.border = mapLayout->border;
        mapLayout->editHistory.clear();
        if (!this->editor->project->saveLayout(mapLayout))
            saved = false;
    }
    if (tileset->hasUnsavedChanges() && !tileset->save())
        saved = false;

    if (this->redrawLayout)
        this->redrawLayout();
    if (this->tilesetEdited)
        this->tilesetEdited();
    if (this->layoutsSaved)
        this->layoutsSaved();
    updateUsage();

    const Stamping::Usage after = Stamping::usage(tileset);
    setStatus(QString("Optimizado: %1 metatiles quitados o juntados, %2 tiles y %3 colores liberados.")
              .arg(out.est.metatilesQuitados).arg(before.tiles - after.tiles).arg(before.colors - after.colors));
    if (!saved)
        RecentErrorMessage::show("El tileset se ha optimizado, pero no se ha podido guardar todo.", this);
    return saved;
}

void PieceStamper::applyLayerVisibility() {
    QList<float> opacity;
    for (const QCheckBox *checkBox : this->checkBoxes_ShowLayer)
        opacity.append((!this->tabOpen || checkBox->isChecked()) ? 1.0 : 0.0);
    if (opacity == Layout::globalMetatileLayerOpacity())
        return;
    Layout::setGlobalMetatileLayerOpacity(opacity);
    if (this->editor->layout && this->redrawLayout)
        this->redrawLayout();
}

void PieceStamper::updateUsage() {
    Layout *layout = this->editor->layout;
    const Tileset *tileset = layout ? layout->tileset_primary : nullptr;
    this->button_Optimize->setEnabled(tileset && this->unsupportedReason.isEmpty());
    if (!tileset) {
        this->label_Usage->clear();
        return;
    }
    Stamping::Usage usage = Stamping::usage(tileset);
    auto row = [](const QString &name, int used, int max) {
        QString count = QString("%1 / %2").arg(used).arg(max);
        if (used >= max) count = QString("<b>%1</b>").arg(count);
        return QString("<tr><td>%1</td><td align=right>&nbsp;%2</td></tr>").arg(name).arg(count);
    };
    this->label_Usage->setText(QString("<b>%1</b><table>%2%3%4</table>")
        .arg(tileset->name.toHtmlEscaped())
        .arg(row("Tiles", usage.tiles, Project::getNumTilesPrimary()))
        .arg(row(QString("Paletas (%1 colores)").arg(usage.colors), usage.palettes, Project::getNumPalettesPrimary()))
        .arg(row("Metatiles", usage.metatiles, Project::getNumMetatilesPrimary())));
}

void PieceStamper::setStatus(const QString &text) {
    this->label_Status->setText(text);
}

void PieceStamper::updatePreview(const QPoint &pos, bool smartPath) {
    LayoutPixmapItem *mapItem = this->editor->map_item;
    if (!mapItem || this->pieceImage.isNull()) {
        hidePreview();
        return;
    }
    if (this->previewParent != mapItem) {
        // A new map item: the old preview went away with the old one, if it's gone.
        if (this->previewParent && this->preview)
            delete this->preview;
        this->preview = new QGraphicsPixmapItem(mapItem);
        this->preview->setOpacity(0.6);
        this->preview->setAcceptedMouseButtons(Qt::NoButton);
        this->previewParent = mapItem;
        this->previewIsSmartPath = !smartPath; // Forces setting the pixmap below
    }
    if (this->previewIsSmartPath != smartPath || this->preview->pixmap().isNull()) {
        this->previewIsSmartPath = smartPath;
        this->preview->setPixmap(QPixmap::fromImage(smartPath ? this->smartPathPreview : this->pieceImage));
    }
    this->preview->setPos(pos);
    this->preview->show();
}

void PieceStamper::hidePreview() {
    if (this->previewParent && this->preview)
        this->preview->hide();
}

void PieceStamper::mapHoverMoved(const QPointF &pos) {
    if (!this->editor->isStampingPieces()) {
        hidePreview();
        return;
    }
    if (isSmartPathMode(QGuiApplication::keyboardModifiers())) {
        QPoint cell = Metatile::coordFromPixmapCoord(pos);
        updatePreview(QPoint(cell.x() * Metatile::pixelWidth(), cell.y() * Metatile::pixelHeight()), true);
    } else {
        updatePreview(snapToGrid(pos), false);
    }
}

void PieceStamper::mapHoverCleared() {
    if (!this->stroking)
        hidePreview();
}
