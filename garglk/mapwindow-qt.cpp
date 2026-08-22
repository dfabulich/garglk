// Copyright (C) 2026 by Dan Fabulich.
//
// Qt map window for the Glk map-document extension.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QCloseEvent>
#include <QFile>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QToolBar>
#include <QWheelEvent>

#include <QSvgRenderer>

#include "garglk.h"
#include "gi_blorb.h"
#include "imgload.h"
#include "map.h"

namespace {

constexpr double kMinScale = 0.2;
constexpr double kMaxScale = 4.0;
constexpr double kZoomFactor = 1.25;

// Qt5's fromTheme(name, fallback) often ignores the fallback: a theme
// icon engine is non-null even when the name is missing. Check
// hasThemeIcon before falling back to the bundled Fluent SVGs.
//
// On Windows Qt5 MinGW, QIcon(":/…svg") yields a non-null icon with no
// availableSizes / null pixmaps (QSvgIconEngine fails). Rasterize with
// QSvgRenderer instead — the same path that successfully draws map SVGs.
QIcon map_icon_from_svg_resource(const QString &resource_path)
{
    QFile file(resource_path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QByteArray svg = file.readAll();
    // QSvgRenderer does not resolve CSS currentColor for icon paints.
    svg.replace("currentColor", "#212121");
    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) {
        return {};
    }
    QSize size = renderer.defaultSize();
    if (size.width() <= 0 || size.height() <= 0) {
        size = QSize(24, 24);
    }
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter painter(&img);
        renderer.render(&painter);
    }
    return QIcon(QPixmap::fromImage(img));
}

QIcon map_toolbar_icon(const QString &theme_name, const QString &resource_path)
{
    if (QIcon::hasThemeIcon(theme_name)) {
        return QIcon::fromTheme(theme_name);
    }
    return map_icon_from_svg_resource(resource_path);
}

QColor map_color_from_rgb(glui32 rgb)
{
    return QColor::fromRgb((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

QImage picture_to_qimage(const picture_t &pic);

QImage load_picture_qimage(glui32 image)
{
    std::vector<unsigned char> raw;
    glui32 chunktype = 0;
    if (gli_picture_copy_raw(image, raw, chunktype)) {
        auto bytes = QByteArray::fromRawData(reinterpret_cast<const char *>(raw.data()), raw.size());
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::ReadOnly);
        const char *format = chunktype == giblorb_ID_JPEG ? "jpeg" : "png";
        QImageReader reader(&buffer, format);
        QImage img = reader.read();
        if (!img.isNull()) {
            return img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        }
    }

    auto pic = gli_picture_load(image);
    if (!pic) {
        return {};
    }
    return picture_to_qimage(*pic);
}

QColor map_canvas_color_from_svg(const QByteArray &svg)
{
    const QString text = QString::fromUtf8(svg);
    const int origin = text.indexOf("<rect x=\"0\" y=\"0\"");
    if (origin < 0) {
        return {};
    }
    const int fill = text.indexOf("fill=\"#", origin);
    if (fill < 0 || fill + 14 > text.size()) {
        return {};
    }
    bool ok = false;
    const unsigned rgb = text.mid(fill + 7, 6).toUInt(&ok, 16);
    if (!ok) {
        return {};
    }
    return map_color_from_rgb(rgb);
}

QImage picture_to_qimage(const picture_t &pic)
{
    QImage img(pic.w, pic.h, QImage::Format_RGBA8888);
    if (img.isNull()) {
        return {};
    }
    const int row_bytes = pic.w * 4;
    if (pic.rgba.stride() == row_bytes) {
        std::memcpy(img.bits(), pic.rgba.data(), row_bytes * pic.h);
    } else {
        for (int y = 0; y < pic.h; y++) {
            std::memcpy(img.scanLine(y), pic.rgba.data() + y * pic.rgba.stride(), row_bytes);
        }
    }
    return img;
}

QImage load_svg_image(const unsigned char *data, glui32 len)
{
    QSvgRenderer renderer(QByteArray(reinterpret_cast<const char *>(data), len));
    if (!renderer.isValid()) {
        return {};
    }
    const QSize size = renderer.defaultSize();
    if (size.width() <= 0 || size.height() <= 0) {
        return {};
    }
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter painter(&img);
    renderer.render(&painter);
    return img;
}

bool point_in_polygon(const QPointF &p, const std::vector<std::pair<glsi32, glsi32>> &pts)
{
    if (pts.size() < 3) {
        return false;
    }
    bool inside = false;
    const std::size_t n = pts.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const double xi = pts[i].first;
        const double yi = pts[i].second;
        const double xj = pts[j].first;
        const double yj = pts[j].second;
        const bool intersect = ((yi > p.y()) != (yj > p.y()))
            && (p.x() < (xj - xi) * (p.y() - yi) / ((yj - yi) + 0.0) + xi);
        if (intersect) {
            inside = !inside;
        }
    }
    return inside;
}

class MapViewportWidget : public QWidget
{
public:
    std::function<void(glui32 link_id)> on_hyperlink;
    std::function<bool()> map_event_armed;

    QImage map_image;
    QColor canvas_color;
    std::vector<map_hyperlink_ui_t> hyperlinks;
    std::vector<map_overlay_ui_t> overlays;
    int focus_index = -1;

    double scale = 1.0;
    double pan_x = 0.0;
    double pan_y = 0.0;

    explicit MapViewportWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
    }

    void recenter_on_focus(const map_focus_rect_t &focus)
    {
        if (map_image.isNull()) {
            return;
        }
        const double pad = 28.0;
        const double fw = std::max(static_cast<double>(focus.width), 1.0);
        const double fh = std::max(static_cast<double>(focus.height), 1.0);
        const double fit = std::min((width() - pad * 2) / fw, (height() - pad * 2) / fh);
        if (scale > fit) {
            scale = std::max(kMinScale, fit);
        }

        const double fx = focus.left + fw / 2.0;
        const double fy = focus.top + fh / 2.0;
        pan_x = width() / 2.0 - fx * scale;
        pan_y = height() / 2.0 - fy * scale;

        const double fl = focus.left * scale + pan_x;
        const double ft = focus.top * scale + pan_y;
        const double fr = (focus.left + fw) * scale + pan_x;
        const double fb = (focus.top + fh) * scale + pan_y;
        if (fl < pad) {
            pan_x += pad - fl;
        } else if (fr > width() - pad) {
            pan_x -= fr - (width() - pad);
        }
        if (ft < pad) {
            pan_y += pad - ft;
        } else if (fb > height() - pad) {
            pan_y -= fb - (height() - pad);
        }
        update();
    }

    void zoom_by(double factor, const QPointF &point_in_view)
    {
        const double before_x = (point_in_view.x() - pan_x) / scale;
        const double before_y = (point_in_view.y() - pan_y) / scale;
        scale = std::min(kMaxScale, std::max(kMinScale, scale * factor));
        pan_x = point_in_view.x() - before_x * scale;
        pan_y = point_in_view.y() - before_y * scale;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), canvas_color.isValid() ? canvas_color : palette().window().color());
        if (map_image.isNull()) {
            return;
        }

        const QRectF dest(pan_x, pan_y, map_image.width() * scale, map_image.height() * scale);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(dest, map_image);

        auto sorted = overlays;
        std::stable_sort(sorted.begin(), sorted.end(),
            [](const map_overlay_ui_t &a, const map_overlay_ui_t &b) {
                return a.zindex < b.zindex;
            });

        for (const auto &ov : sorted) {
            double ow = ov.width;
            double oh = ov.height;
            QImage oimg;
            if (ov.is_fill) {
                ow = ov.width;
                oh = ov.height;
                if (ow <= 0 || oh <= 0) {
                    continue;
                }
                painter.fillRect(QRectF(pan_x + ov.left * scale, pan_y + ov.top * scale, ow * scale, oh * scale),
                    map_color_from_rgb(ov.color));
                continue;
            }
            oimg = load_picture_qimage(ov.image_id);
            if (oimg.isNull()) {
                continue;
            }
            if (ow <= 0) {
                ow = oimg.width();
            }
            if (oh <= 0) {
                oh = oimg.height();
            }
            painter.drawImage(QRectF(pan_x + ov.left * scale, pan_y + ov.top * scale, ow * scale, oh * scale), oimg);
        }

        const int n = focusable_count();
        if (focus_index >= 0 && focus_index < n) {
            painter.setPen(QPen(QColor(26, 89, 217, 230), 2));
            painter.setBrush(QColor(26, 115, 242, 90));
            if (focus_index < static_cast<int>(hyperlinks.size())) {
                const auto &h = hyperlinks[focus_index];
                if (h.points.size() >= 3) {
                    QPolygonF poly;
                    for (const auto &pt : h.points) {
                        poly << QPointF(pan_x + pt.first * scale, pan_y + pt.second * scale);
                    }
                    painter.drawPolygon(poly);
                }
            } else {
                const auto linked = linked_overlays();
                const int oi = focus_index - static_cast<int>(hyperlinks.size());
                if (oi >= 0 && oi < static_cast<int>(linked.size())) {
                    const QRectF r = overlay_doc_rect(linked[oi]);
                    painter.drawRect(QRectF(pan_x + r.left() * scale, pan_y + r.top() * scale,
                        r.width() * scale, r.height() * scale));
                }
            }
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            dragging = true;
            did_drag = false;
            drag_start = event->pos();
            drag_last = event->pos();
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const QPointF doc = document_point_from_view(event->pos());
        if (link_id_at_document(doc) != 0) {
            setCursor(Qt::PointingHandCursor);
        } else {
            unsetCursor();
        }

        if (!dragging) {
            return;
        }
        const QPointF delta = event->pos() - drag_start;
        if (!did_drag && (delta.x() * delta.x() + delta.y() * delta.y()) < 16.0) {
            return;
        }
        did_drag = true;
        pan_x += event->pos().x() - drag_last.x();
        pan_y += event->pos().y() - drag_last.y();
        drag_last = event->pos();
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (!dragging || event->button() != Qt::LeftButton) {
            dragging = false;
            return;
        }
        dragging = false;
        if (did_drag) {
            return;
        }
        if (!map_event_armed || !map_event_armed()) {
            return;
        }
        const glui32 link_id = link_id_at_document(document_point_from_view(event->pos()));
        if (link_id != 0 && on_hyperlink) {
            on_hyperlink(link_id);
        }
    }

    void wheelEvent(QWheelEvent *event) override
    {
        const double factor = event->angleDelta().y() > 0 ? kZoomFactor : 1.0 / kZoomFactor;
        zoom_by(factor, event->position());
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        const int n = focusable_count();
        if (event->key() == Qt::Key_Tab && n > 0) {
            if (focus_index < 0) {
                focus_index = event->modifiers() & Qt::ShiftModifier ? n - 1 : 0;
            } else if (event->modifiers() & Qt::ShiftModifier) {
                focus_index = (focus_index + n - 1) % n;
            } else {
                focus_index = (focus_index + 1) % n;
            }
            update();
            return;
        }
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space)
            && focus_index >= 0 && focus_index < n) {
            if (map_event_armed && map_event_armed()) {
                glui32 link_id = 0;
                if (focus_index < static_cast<int>(hyperlinks.size())) {
                    link_id = hyperlinks[focus_index].id;
                } else {
                    const auto linked = linked_overlays();
                    const int oi = focus_index - static_cast<int>(hyperlinks.size());
                    if (oi >= 0 && oi < static_cast<int>(linked.size())) {
                        link_id = linked[oi].link_id;
                    }
                }
                if (link_id != 0 && on_hyperlink) {
                    on_hyperlink(link_id);
                }
            }
            return;
        }
        QWidget::keyPressEvent(event);
    }

private:
    bool dragging = false;
    bool did_drag = false;
    QPointF drag_start;
    QPointF drag_last;

    QPointF document_point_from_view(const QPointF &view_pt) const
    {
        if (scale <= 0) {
            return {};
        }
        return {(view_pt.x() - pan_x) / scale, (view_pt.y() - pan_y) / scale};
    }

    QRectF overlay_doc_rect(const map_overlay_ui_t &ov) const
    {
        double ow = ov.width;
        double oh = ov.height;
        if (!ov.is_fill) {
            auto pic = gli_picture_load(ov.image_id);
            if (pic) {
                if (ow <= 0) {
                    ow = pic->w;
                }
                if (oh <= 0) {
                    oh = pic->h;
                }
            }
        }
        return {static_cast<qreal>(ov.left), static_cast<qreal>(ov.top),
                static_cast<qreal>(ow), static_cast<qreal>(oh)};
    }

    std::vector<map_overlay_ui_t> linked_overlays() const
    {
        std::vector<map_overlay_ui_t> out;
        for (const auto &ov : overlays) {
            if (ov.link_id != 0) {
                out.push_back(ov);
            }
        }
        std::stable_sort(out.begin(), out.end(),
            [](const map_overlay_ui_t &a, const map_overlay_ui_t &b) {
                return a.zindex < b.zindex;
            });
        return out;
    }

    int focusable_count() const
    {
        return static_cast<int>(hyperlinks.size() + linked_overlays().size());
    }

    glui32 link_id_at_document(const QPointF &doc) const
    {
        const auto linked = linked_overlays();
        for (auto it = linked.rbegin(); it != linked.rend(); ++it) {
            if (overlay_doc_rect(*it).contains(doc)) {
                return it->link_id;
            }
        }
        for (auto it = hyperlinks.rbegin(); it != hyperlinks.rend(); ++it) {
            if (point_in_polygon(doc, it->points)) {
                return it->id;
            }
        }
        return 0;
    }
};

class MapWindow : public QMainWindow
{
public:
    MapViewportWidget *viewport = nullptr;
    bool visible_flag = false;
    bool suggested_once = false;

    QImage latent_bitmap;
    QByteArray latent_svg;
    glui32 latent_bgcolor = mapcolor_Default;
    std::vector<map_hyperlink_ui_t> latent_hyperlinks;
    std::vector<map_overlay_ui_t> overlay_list;
    std::optional<map_focus_rect_t> focus;

    MapWindow()
    {
        setWindowTitle(tr("Map"));
        resize(720, 640);
        setMinimumSize(200, 160);

        viewport = new MapViewportWidget(this);
        viewport->map_event_armed = []() { return gli_map_event_request; };
        viewport->on_hyperlink = [](glui32 link_id) {
            gli_map_post_event(mapevent_Hyperlink, link_id);
        };
        setCentralWidget(viewport);

        auto *toolbar = addToolBar(tr("Map"));
        toolbar->setMovable(false);
        toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        const QIcon zoom_in_icon = map_toolbar_icon(
            QStringLiteral("zoom-in"), QStringLiteral(":/icons/zoom-in.svg"));
        const QIcon zoom_out_icon = map_toolbar_icon(
            QStringLiteral("zoom-out"), QStringLiteral(":/icons/zoom-out.svg"));
        auto *zoom_in = toolbar->addAction(zoom_in_icon, tr("Zoom In"));
        auto *zoom_out = toolbar->addAction(zoom_out_icon, tr("Zoom Out"));
        connect(zoom_in, &QAction::triggered, this, [this]() {
            const QPointF center(viewport->width() / 2.0, viewport->height() / 2.0);
            viewport->zoom_by(kZoomFactor, center);
        });
        connect(zoom_out, &QAction::triggered, this, [this]() {
            const QPointF center(viewport->width() / 2.0, viewport->height() / 2.0);
            viewport->zoom_by(1.0 / kZoomFactor, center);
        });

        update_title();
    }

    bool has_document() const
    {
        return !latent_svg.isEmpty() || !latent_bitmap.isNull();
    }

    bool map_visible() const
    {
        return visible_flag && isVisible();
    }

    void render_document()
    {
        if (!latent_bitmap.isNull()) {
            viewport->map_image = latent_bitmap;
        } else if (!latent_svg.isEmpty()) {
            viewport->map_image = load_svg_image(
                reinterpret_cast<const unsigned char *>(latent_svg.constData()),
                latent_svg.size());
        } else {
            viewport->map_image = QImage();
        }

        if (latent_bgcolor != mapcolor_Default) {
            viewport->canvas_color = map_color_from_rgb(latent_bgcolor);
        } else if (!latent_svg.isEmpty()) {
            viewport->canvas_color = map_canvas_color_from_svg(latent_svg);
        } else {
            viewport->canvas_color = QColor();
        }

        viewport->hyperlinks = latent_hyperlinks;
        viewport->overlays = overlay_list;
        viewport->update();
    }

    void prepare_beside_game(QMainWindow *game_win)
    {
        if (game_win == nullptr || isVisible()) {
            return;
        }
        const QRect gf = game_win->frameGeometry();
        QRect mf = frameGeometry();
        mf.moveLeft(gf.right() + 12);
        mf.moveTop(gf.top());
        setGeometry(mf);
    }

    void show_internal(QMainWindow *game_win, bool take_key)
    {
        if (!has_document()) {
            return;
        }
        render_document();
        prepare_beside_game(game_win);
        if (take_key) {
            show();
            raise();
            activateWindow();
            viewport->setFocus();
        } else {
            show();
            if (game_win != nullptr) {
                game_win->raise();
                game_win->activateWindow();
            }
        }
        visible_flag = true;
        if (focus.has_value()) {
            viewport->recenter_on_focus(*focus);
        }
    }

    void hide_internal()
    {
        visible_flag = false;
        hide();
    }

    void present_with_flags(glui32 flags)
    {
        if (map_visible()) {
            render_document();
        } else {
            viewport->hyperlinks = latent_hyperlinks;
            viewport->overlays = overlay_list;
        }

        extern QMainWindow *g_game_window;
        QMainWindow *game_win = g_game_window;

        if ((flags & mapflag_UserRequestedShow) != 0) {
            show_internal(game_win, false);
            if (focus.has_value() && (flags & mapflag_HasFocus) != 0) {
                viewport->recenter_on_focus(*focus);
            }
        } else if ((flags & mapflag_SuggestShow) != 0 && !suggested_once) {
            suggested_once = true;
            show_internal(game_win, false);
            if (focus.has_value()) {
                viewport->recenter_on_focus(*focus);
            }
        } else if (map_visible()) {
            if ((flags & mapflag_HasFocus) != 0 && focus.has_value()) {
                viewport->recenter_on_focus(*focus);
            }
        }
    }

    void update_title()
    {
        QString title;
        if (!gli_story_title.empty()) {
            title = QString::fromStdString(gli_story_title);
        } else if (!gli_story_name.empty()) {
            title = QString::fromStdString(gli_story_name);
        }
        if (!title.isEmpty()) {
            setWindowTitle(tr("Map - %1").arg(title));
        } else {
            setWindowTitle(tr("Map"));
        }
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        event->ignore();
        const bool was_visible = map_visible();
        hide_internal();
        if (was_visible) {
            gli_map_post_event(mapevent_UserHide, 0);
        }
    }
};

MapWindow *g_map_window = nullptr;
QMainWindow *g_game_window = nullptr;

MapWindow *ensure_map_window()
{
    if (g_map_window == nullptr) {
        g_map_window = new MapWindow;
    }
    return g_map_window;
}

} // namespace

void gli_map_set_game_window(QMainWindow *win)
{
    g_game_window = win;
}

void gli_map_ui_present_svg(const unsigned char *data, glui32 len,
    glui32 flags, glui32 bgcolor, const map_focus_rect_t *focus_rect,
    const std::vector<map_hyperlink_ui_t> &hyperlinks)
{
    auto *map = ensure_map_window();
    map->overlay_list.clear();
    map->latent_bitmap = QImage();
    map->latent_svg = QByteArray(reinterpret_cast<const char *>(data), len);
    map->latent_bgcolor = bgcolor;
    map->latent_hyperlinks = hyperlinks;
    if ((flags & mapflag_HasFocus) != 0 && focus_rect != nullptr) {
        map->focus = *focus_rect;
    } else if (focus_rect != nullptr) {
        map->focus = *focus_rect;
    }
    map->update_title();
    map->present_with_flags(flags);
}

void gli_map_ui_present_image(glui32 image, glui32 flags, glui32 bgcolor,
    const map_focus_rect_t *focus_rect, const std::vector<map_hyperlink_ui_t> &hyperlinks)
{
    QImage bitmap = load_picture_qimage(image);
    if (bitmap.isNull()) {
        return;
    }
    auto *map = ensure_map_window();
    map->overlay_list.clear();
    map->latent_svg.clear();
    map->latent_bitmap = std::move(bitmap);
    map->latent_bgcolor = bgcolor;
    map->latent_hyperlinks = hyperlinks;
    if (focus_rect != nullptr) {
        map->focus = *focus_rect;
    }
    map->update_title();
    map->present_with_flags(flags);
}

void gli_map_ui_set_hyperlinks(const std::vector<map_hyperlink_ui_t> &hyperlinks)
{
    if (g_map_window == nullptr) {
        return;
    }
    g_map_window->latent_hyperlinks = hyperlinks;
    g_map_window->viewport->hyperlinks = hyperlinks;
    g_map_window->viewport->update();
}

void gli_map_ui_overlay(const map_overlay_ui_t &overlay)
{
    auto *map = ensure_map_window();
    for (auto &ov : map->overlay_list) {
        if (ov.overlay_id == overlay.overlay_id) {
            ov = overlay;
            map->viewport->overlays = map->overlay_list;
            map->viewport->update();
            return;
        }
    }
    map->overlay_list.push_back(overlay);
    map->viewport->overlays = map->overlay_list;
    map->viewport->update();
}

void gli_map_ui_overlay_move(overlayid_t overlay, glsi32 left, glsi32 top,
    glui32 width, glui32 height, glui32 zindex)
{
    if (g_map_window == nullptr) {
        return;
    }
    for (auto &ov : g_map_window->overlay_list) {
        if (ov.overlay_id == overlay) {
            ov.left = left;
            ov.top = top;
            ov.width = width;
            ov.height = height;
            ov.zindex = zindex;
            g_map_window->viewport->overlays = g_map_window->overlay_list;
            g_map_window->viewport->update();
            return;
        }
    }
}

void gli_map_ui_overlay_clear(overlayid_t overlay)
{
    if (g_map_window == nullptr) {
        return;
    }
    auto &list = g_map_window->overlay_list;
    list.erase(std::remove_if(list.begin(), list.end(),
        [overlay](const map_overlay_ui_t &ov) { return ov.overlay_id == overlay; }),
        list.end());
    g_map_window->viewport->overlays = list;
    g_map_window->viewport->update();
}

void gli_map_ui_overlay_clear_all()
{
    if (g_map_window == nullptr) {
        return;
    }
    g_map_window->overlay_list.clear();
    g_map_window->viewport->overlays.clear();
    g_map_window->viewport->update();
}

void gli_map_ui_close()
{
    if (g_map_window == nullptr) {
        return;
    }
    g_map_window->latent_svg.clear();
    g_map_window->latent_bitmap = QImage();
    g_map_window->latent_bgcolor = mapcolor_Default;
    g_map_window->latent_hyperlinks.clear();
    g_map_window->overlay_list.clear();
    g_map_window->focus.reset();
    g_map_window->viewport->map_image = QImage();
    g_map_window->viewport->canvas_color = QColor();
    g_map_window->viewport->hyperlinks.clear();
    g_map_window->viewport->overlays.clear();
    g_map_window->viewport->focus_index = -1;
    g_map_window->viewport->scale = 1.0;
    g_map_window->viewport->pan_x = 0.0;
    g_map_window->viewport->pan_y = 0.0;
    g_map_window->suggested_once = false;
    g_map_window->hide_internal();
}

void gli_map_ui_set_focus(const map_focus_rect_t *focus)
{
    if (g_map_window == nullptr || focus == nullptr) {
        return;
    }
    g_map_window->focus = *focus;
    if (g_map_window->map_visible()) {
        g_map_window->viewport->recenter_on_focus(*focus);
    }
}

void gli_map_ui_clear_focus()
{
    if (g_map_window == nullptr) {
        return;
    }
    g_map_window->focus.reset();
}

void gli_map_ui_set_event_request(bool enabled)
{
    (void)enabled;
}
