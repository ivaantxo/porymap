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
class QTimer;

// The image shown in the Pieces tab, zoomed in. A region of it is picked on the
// 8x8 grid, and that region is the piece that gets stamped.
class PieceSheetItem : public SelectablePixmapItem {
    Q_OBJECT
public:
    static constexpr int zoom = 2;

    PieceSheetItem() : SelectablePixmapItem(Tile::pixelWidth() * zoom, Tile::pixelHeight() * zoom) {}
    void setImage(const QImage &image);
    void setFrame(const QImage &image); // Same size, keeping the selection (an animation playing)
    QRect selectedRect() const; // In pixels of the image
    void draw() override;

private:
    QPixmap basePixmap;
    QPixmap makePixmap(const QImage &image) const;
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

    QLabel *label_Limited;
    QComboBox *comboBox_Pieces;
    QPushButton *button_Import;
    QPushButton *button_Remove;
    QGraphicsScene *sheetScene;
    QGraphicsView *sheetView;
    PieceSheetItem *sheetItem;
    QLabel *label_Piece;
    QComboBox *comboBox_Animations;
    QPushButton *button_ImportAnimation;
    QPushButton *button_RemoveAnimation;
    QTimer *animationTimer;
    QList<QImage> animationFramesShown;
    int animationFrame = 0;
    QButtonGroup *layerGroup;
    QCheckBox *checkBox_Replace;
    QComboBox *comboBox_Grid;
    QList<QCheckBox*> checkBoxes_ShowLayer;
    QLabel *label_Usage;
    QPushButton *button_Optimize;
    QLabel *label_Status;

    bool tabOpen = false;
    QMetaObject::Connection historyConnection;
    QString pieceListDir; // Project whose piece list is loaded
    QString unsupportedReason;
    QImage sheetSource;  // As loaded: with a palette, the piece keeps its indices
    QImage sheetImage;   // As shown
    QImage pieceImage;
    mapeado::Imagen piece;

    QGraphicsPixmapItem *preview = nullptr;
    QPointer<LayoutPixmapItem> previewParent;

    // A 48x48 piece is also a smart path: its 3x3 cells, in the same order as a 3x3
    // smart path selection of metatiles.
    mapeado::Imagen smartPathPieces[9];
    QImage smartPathPreview; // The open (middle) cell, 2x2, which is what each step paints
    bool previewIsSmartPath = false;

    unsigned actionId = 0;
    QPoint strokeOrigin;
    QPoint lastStampPos; // In pixels, or in cells for smart paths
    bool stroking = false;
    bool strokeSmartPath = false;
    bool strokeFailed = false;

    void importPieces();
    void removePiece();
    void loadPieceList();
    void savePieceList();
    void loadPiece(int index);
    void updatePiece();

    // The tileset's animations: playing in the sheet, frame 0 is the piece.
    void refreshAnimations();
    void loadAnimation(int index);
    void stopAnimation();
    void importAnimation();
    void removeAnimation();

    QPoint snapToGrid(const QPointF &pos) const;
    QPoint strokePos(const QPointF &pos) const;
    bool isSmartPathMode(Qt::KeyboardModifiers modifiers) const;
    bool stampAt(const QPoint &pos);
    bool stampSmartPathAt(const QPoint &cell);

    struct StampJob;
    bool beginStamp(StampJob *job);
    bool addStamp(StampJob &job, const mapeado::Imagen &piece, const QPoint &pos, bool replace);
    bool isSmartPathCell(const StampJob &job, const QPoint &cell) const;
    bool finishStamp(StampJob &job, const std::function<void()> &retry);
    // Changes the tileset outside a stroke (animations), the same way a stamp would.
    bool editTileset(const std::function<mapeado::Estampado(StampJob &)> &edit, const QString &failure);
    void showStampFailure(const mapeado::Estampado &result, const std::function<void()> &retry);
    bool optimize();

    void applyLayerVisibility();
    void updateUsage();
    void setStatus(const QString &text);
    void updatePreview(const QPoint &pos, bool smartPath);
    void hidePreview();
};

#endif // PIECESTAMPER_H
