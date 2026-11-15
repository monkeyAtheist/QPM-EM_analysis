#pragma once

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QSizeF>
#include <QPointF>
#include <QRectF>
#include <QVector>
#include <QWidget>

#include <functional>
#include <utility>

class QMouseEvent;
class QTabletEvent;
class QPaintEvent;
class QPainter;
class QWheelEvent;
class QResizeEvent;
class QEvent;

// One editable notebook: several pages, freehand ink, and positioned text.
// Ink schema v1/v2 is migrated on read; project .qta schema is unaffected.
class NotesInkCanvas final : public QWidget
{
public:
    enum class Tool { Pen, Eraser, Text, Select, Line, Arrow, Rectangle, Ellipse, Hand, Component, Wire };
    explicit NotesInkCanvas(QWidget *parent = nullptr);
    void setOnModified(std::function<void()> callback) { m_onModified = std::move(callback); }
    void setOnViewChanged(std::function<void(double)> callback) { m_onViewChanged = std::move(callback); }
    void setTool(Tool tool);
    void setPenColor(const QColor &color);
    void setPenWidth(double width);
    void setTextSize(int size);
    void setGridVisible(bool visible);
    bool gridVisible() const noexcept { return m_grid; }
    bool canUndo() const noexcept;
    bool canRedo() const noexcept;
    int strokeCount() const noexcept;
    int textCount() const noexcept;
    int shapeCount() const noexcept;
    int imageCount() const noexcept;
    int symbolCount() const noexcept;
    int wireCount() const noexcept;
    int selectedObjectCount() const noexcept;
    void selectAllObjects();
    bool addWire(const QPointF &from, const QPointF &to); // auto-anchor endpoints to nearest pins
    void setSymbolType(const QString &id);
    void setSymbolScale(double scale);
    void setSymbolSnap(bool enabled) noexcept { m_symbolSnap = enabled; }
    bool symbolSnap() const noexcept { return m_symbolSnap; }
    // Stored as editable vector objects, not raster images or electrical netlist elements.
    bool addSymbol(const QString &id, const QPointF &centre, double scale = 1.0,
                   int rotationDegrees = 0, const QString &reference = QString());
    bool rotateSelectedSymbol(int degrees = 90);
    bool resizeSelectedSymbol(double scale);
    bool renameSelectedSymbol(const QString &reference);
    bool hasSelectedSymbol() const noexcept;
    QString selectedSymbolReference() const;
    bool deleteSelectedSymbol();
    // View navigation is intentionally NOT included in notebook serialization or Undo.
    double zoomFactor() const noexcept { return m_zoom; }
    void setZoomFactor(double factor, const QPointF &anchor = QPointF(-1, -1));
    void zoomIn();
    void zoomOut();
    void fitPage();
    QPointF pageToWidget(const QPointF &pagePoint) const;
    QPointF widgetToPage(const QPointF &widgetPoint) const { return mapToPage(widgetPoint); }
    bool addImage(const QImage &image, const QPointF &pagePosition = QPointF(-1, -1));

    void addShape(Tool type, const QPointF &start, const QPointF &end);
    int pageCount() const noexcept { return m_pages.size(); }
    int currentPageIndex() const noexcept { return m_currentPage; }
    bool setCurrentPage(int index);
    bool addPage();
    bool removeCurrentPage(); // at least one page is always retained
    void addTextAt(const QPointF &pagePosition, const QString &text); // also used by Qt tests
    void undo();
    void redo();
    void clearDocument(); // current page only
    QByteArray serialize() const;
    bool loadDocument(const QByteArray &data, QString *error = nullptr);
    QImage renderToImage(int width = 1600, int height = 1000) const; // active page

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void tabletEvent(QTabletEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent *event) override;

private:
    struct Sample { QPointF pos; double pressure = 1.0; };
    struct Stroke { QColor color; double width = 3.0; QVector<Sample> samples; };
    struct TextItem { QPointF pos; QString text; QColor color; int size = 32; };
    struct Shape
    {
        Tool type = Tool::Line;
        QPointF a, b;
        QColor color;
        double width = 3.0;
        quint64 aSymbol = 0, bSymbol = 0; // stable symbol IDs; 0 means free endpoint
        int aPin = -1, bPin = -1;
    };
    struct ImageItem { QPointF pos; QSizeF size; QByteArray png; QImage pixels; };
    struct SymbolItem
    {
        QString id;
        QPointF center;
        double scale = 1.0;
        int rotation = 0; // clockwise quarter turns in page coordinates
        QColor color{27, 54, 86};
        double lineWidth = 3.0;
        QString reference;
        quint64 uid = 0; // persistent ID; survives rename, move and page save/load
    };
    enum class SelectionKind { None, Text, Shape, Stroke, Image, Symbol };
    struct SelectionRef { SelectionKind kind = SelectionKind::None; int index = -1; };
    struct ContentState
    {
        QVector<Stroke> strokes;
        QVector<TextItem> texts;
        QVector<Shape> shapes;
        QVector<ImageItem> images;
        QVector<SymbolItem> symbols;
    };
    struct Edit
    {
        enum class Type { AddStroke, RemoveStroke, AddText, RemoveText, ChangeText,
                          AddShape, RemoveShape, MoveText, MoveShape, MoveStroke, AddImage, RemoveImage, MoveImage, ResizeImage, AddSymbol, RemoveSymbol, ChangeSymbol, MoveGroup, Clear } type = Type::AddStroke;
        int index = -1;
        Stroke stroke;
        Stroke beforeStroke;
        Stroke afterStroke;
        Shape shape;
        Shape beforeShape;
        Shape afterShape;
        TextItem beforeText;
        TextItem afterText;
        QVector<Stroke> previousStrokes;
        QVector<TextItem> previousTexts;
        QVector<Shape> previousShapes;
        ImageItem image;
        ImageItem beforeImage;
        ImageItem afterImage;
        QVector<ImageItem> previousImages;
        SymbolItem symbol;
        SymbolItem beforeSymbol;
        SymbolItem afterSymbol;
        QVector<SymbolItem> previousSymbols;
        QVector<Shape> beforeShapes, afterShapes;
        ContentState beforeGroup, afterGroup;
    };
    struct Page
    {
        QVector<Stroke> strokes;
        QVector<TextItem> texts;
        QVector<Shape> shapes;
        QVector<ImageItem> images;
        QVector<SymbolItem> symbols;
        QVector<Edit> undo;
        QVector<Edit> redo;
    };
    Page &page() { return m_pages[m_currentPage]; }
    const Page &page() const { return m_pages[m_currentPage]; }
    QPointF mapToPage(const QPointF &widgetPos) const;
    QRectF pageRect() const;
    QRectF textBounds(const TextItem &item) const;
    int textAt(const QPointF &paperPos) const;
    int shapeAt(const QPointF &paperPos) const;
    int strokeAt(const QPointF &paperPos) const;
    int imageAt(const QPointF &paperPos) const;
    int symbolAt(const QPointF &paperPos) const;
    QVector<QPointF> localPins(const QString &symbolId) const;
    QPointF pinLocation(const SymbolItem &symbol, int pin) const;
    QPointF anchoredPoint(quint64 symbolUid, int pin, const QPointF &fallback) const;
    Shape anchoredWire(const Shape &wire) const;
    bool snapToTerminal(const QPointF &point, quint64 &uid, int &pin, QPointF &snapped) const;
    QRectF boundsFor(const SelectionRef &ref) const;
    bool selectedRef(const SelectionRef &ref) const;
    bool removeSymbolAt(int index);
    ContentState snapshot() const;
    void applySnapshot(const ContentState &state);
    void collectMarquee(const QRectF &area);
    QRectF symbolBounds(const SymbolItem &item) const;
    static bool knownSymbol(const QString &id);
    static QString symbolDesignator(const QString &id);
    QPointF snapSymbolPoint(QPointF point) const;
    void drawSymbol(QPainter &painter, const SymbolItem &symbol) const;
    QRectF selectionBounds() const;
    void resetSelection();
    void beginSelection(const QPointF &paperPos, bool additive = false);
    void moveSelection(const QPointF &paperPos);
    void resizeSelectedImage(const QPointF &paperPos, bool freeAspect);
    static bool isShapeTool(Tool tool);
    static QRectF strokeBounds(const Stroke &stroke);
    void editTextAt(const QPointF &paperPos);
    void beginStroke(const QPointF &pos, double pressure, bool additive = false);
    void continueStroke(const QPointF &pos, double pressure, bool constrain = false);
    void endStroke();
    void eraseAt(const QPointF &pos);
    void commitEdit(Edit edit);
    void modified();
    void drawContent(QPainter &painter, bool drawPaper) const;
    void drawStroke(QPainter &painter, const Stroke &stroke) const;
    void drawShape(QPainter &painter, const Shape &shape) const;
    void clampPan();
    void beginPan(const QPointF &widgetPosition);
    void movePan(const QPointF &widgetPosition);
    void endPan();
    static constexpr double PageW = 1600.0;
    static constexpr double PageH = 1000.0;
    static constexpr int MaxPages = 32;
    QVector<Page> m_pages{Page{}};
    int m_currentPage = 0;
    Stroke m_activeStroke;
    Shape m_activeShape;
    bool m_shapeDrawing = false;
    SelectionKind m_selection = SelectionKind::None;
    int m_selectionIndex = -1;
    bool m_movingSelection = false;
    bool m_resizingImage = false;
    QPointF m_dragOrigin;
    TextItem m_originalText;
    Shape m_originalShape;
    Stroke m_originalStroke;
    ImageItem m_originalImage;
    SymbolItem m_originalSymbol;
    ContentState m_groupOrigin;
    QVector<SelectionRef> m_groupSelection;
    bool m_marqueeSelecting = false;
    QRectF m_marquee;
    bool m_wireDrawing = false;
    Shape m_activeWire;
    quint64 m_nextSymbolUid = 1;
    bool m_drawing = false;
    bool m_panning = false;
    QPointF m_panOrigin;
    QPointF m_panStart;
    QPointF m_viewPan;
    double m_zoom = 1.0; // 1.0 means fit page, up to 6x fitted size
    bool m_touchPinching = false;
    double m_touchPinchDistance = 0.0;
    double m_touchStartZoom = 1.0;
    QPointF m_touchPreviousCenter;
    bool m_erasing = false;
    bool m_tabletStroke = false;
    bool m_tabletEraser = false;
    Tool m_tool = Tool::Pen;
    QColor m_color{27, 54, 86};
    double m_width = 3.5;
    int m_textSize = 32;
    bool m_grid = true;
    QString m_symbolType = QStringLiteral("resistor");
    double m_symbolScale = 1.0;
    bool m_symbolSnap = true;
    std::function<void()> m_onModified;
    std::function<void(double)> m_onViewChanged;
};
