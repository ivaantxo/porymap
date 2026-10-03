#pragma once
#ifndef PIECESTAMPER_H
#define PIECESTAMPER_H

#include "mapeado.h"
#include "selectablepixmapitem.h"
#include "tile.h"

#include <QPointer>
#include <QWidget>
#include <functional>

class Editor;
class LayoutPixmapItem;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QGraphicsPixmapItem;
class QGraphicsScene;
class QGraphicsSceneMouseEvent;
class QGraphicsView;
class QLabel;
class QPushButton;

// The image shown in the Pieces tab, zoomed in. A region of it is picked on the
// 8x8 grid, and that region is the piece that gets stamped.
class PieceSheetItem : public SelectablePixmapItem {
    Q_OBJECT
public:
    static constexpr int zoom = 2;

    PieceSheetItem() : SelectablePixmapItem(Tile::pixelWidth() * zoom, Tile::pixelHeight() * zoom) {}
    void setImage(const QImage &image);
    QRect selectedRect() const; // In pixels of the image
    void draw() override;

private:
    QPixmap basePixmap;
};

// The Pieces tab, and the stamping tool that goes with it. With the tab open and the
// pencil selected, clicking the map stamps the selected piece onto the chosen layer,
// and dragging repeats it. Whatever the piece needs (colors, tiles, metatiles) is added
// to the layout's primary tileset right then; if it doesn't fit, nothing is painted
// and a warning says what's missing.
class PieceStamper : public QWidget {
    Q_OBJECT
public:
    explicit PieceStamper(Editor *editor, QWidget *parent = nullptr);

    // Set up by MainWindow.
    std::function<bool()> canEditTileset; // False if the Tileset Editor has unsaved edits
    std::function<void()> tilesetEdited;  // After stamping or optimizing changed the tileset
    std::function<void()> layoutsSaved;   // After optimizing saved the layouts
    std::function<void()> redrawLayout;   // Redraw the map and the metatile views from scratch

    void setTabOpen(bool open);
    bool isActive() const; // The tab is open and there's a piece to stamp
    void refresh();        // The layout or its tilesets changed

    void mapMouseEvent(QGraphicsSceneMouseEvent *event, LayoutPixmapItem *item);
    void mapHoverMoved(const QPointF &pos);
    void mapHoverCleared();

private:
    Editor *editor;

    QComboBox *comboBox_Pieces;
    QPushButton *button_Import;
    QPushButton *button_Remove;
    QGraphicsScene *sheetScene;
    QGraphicsView *sheetView;
    PieceSheetItem *sheetItem;
    QLabel *label_Piece;
    QButtonGroup *layerGroup;
    QCheckBox *checkBox_Replace;
    QComboBox *comboBox_Grid;
    QList<QCheckBox*> checkBoxes_ShowLayer;
    QLabel *label_Usage;
    QPushButton *button_Optimize;
    QLabel *label_Status;

    bool tabOpen = false;
    QString pieceListDir; // Project whose piece list is loaded
    QString unsupportedReason;
    QImage sheetImage;
    QImage pieceImage;
    mapeado::Imagen piece;

    QGraphicsPixmapItem *preview = nullptr;
    QPointer<LayoutPixmapItem> previewParent;

    unsigned actionId = 0;
    QPoint strokeOrigin;
    QPoint lastStampPos;
    bool stroking = false;
    bool strokeFailed = false;

    void importPieces();
    void removePiece();
    void loadPieceList();
    void savePieceList();
    void loadPiece(int index);
    void updatePiece();

    QPoint snapToGrid(const QPointF &pos) const;
    QPoint strokePos(const QPointF &pos) const;
    bool stampAt(const QPoint &pos);
    void showStampFailure(const mapeado::Estampado &result, const QPoint &pos);
    bool optimize();

    void applyLayerVisibility();
    void updateUsage();
    void setStatus(const QString &text);
    void updatePreview(const QPoint &pos);
    void hidePreview();
};

#endif // PIECESTAMPER_H
