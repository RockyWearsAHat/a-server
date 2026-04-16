# Qt 6 Patterns and Pitfalls

## Scope

Best practices, ownership rules, signal-slot patterns, and common mistakes
for Qt 6 C++ widget development. Applies to all `src/gui/` and
`include/gui/` code in AIO Server.

## Object Trees and Ownership

Qt has its own memory management model that coexists with (and sometimes
conflicts with) standard C++ RAII.

### The Parent-Child Rule

- When you create a `QObject` with a parent, the parent takes ownership.
- When the parent is destroyed, it destroys all its children.
- A child removes itself from its parent's list when destroyed.
- **No `QObject` is deleted twice** — the tree handles this.

### The Stack Allocation Trap

```cpp
// CORRECT — child destroyed before parent (reverse order)
QWidget window;
QPushButton quit("Quit", &window);

// BUG — parent destroyed first, tries to delete quit,
// then quit's stack destructor fires → double-free
QPushButton quit("Quit");
QWidget window;
quit.setParent(&window);
```

**Rule**: if QObjects are on the stack, their destruction order must be
reverse of their parent-child assignment. Safest approach: allocate children
on the heap with `new` and let the parent tree own them.

### `deleteLater()` — The Safe Delete

- `deleteLater()` defers deletion to the next event loop iteration.
- **Required** when deleting an object from within its own signal handler
  or event handler (deleting `this` during a signal emission is UB).
- Safe to call multiple times — only the first queued delete executes.

### When Qt Ownership Clashes with Smart Pointers

- **Don't** wrap parent-owned children in `unique_ptr` — the parent will
  try to delete them, and so will the `unique_ptr` → double-free.
- For non-parented QObjects, `unique_ptr` is fine.
- Prefer raw `new` with parent ownership over fighting the Qt tree model.

## Signals and Slots

### Best Practices

- **Prefer new-style connections** (function pointers):
  ```cpp
  connect(sender, &Sender::signal, receiver, &Receiver::slot);
  ```
  These are checked at compile time. The old `SIGNAL()`/`SLOT()` macros
  only fail at runtime.
- **Keep signal argument types generic** — use `int`, `QString`, etc.
  Custom types couple the signal to a specific class, reducing reusability.
- **Signals and slots are ~10× slower than direct calls** — but this is
  negligible compared to any heap allocation, I/O, or paint operation.
- **Queued connections** across threads: the arguments are copied. Ensure
  types are registered with `qRegisterMetaType<T>()` for custom types.
- **Don't emit signals from constructors** — connected slots may not be
  ready yet because the object isn't fully constructed.

### Signal-Slot vs Direct Calls

| Relationship      | Use                  |
| ----------------- | -------------------- |
| Parent → child    | Direct function call |
| Child → parent    | Signal → slot        |
| Sibling → sibling | Signal → slot        |
| Cross-thread      | Queued signal → slot |

Direct calls from parent to child are fine — the parent owns the child and
knows its type. Signals are for loose coupling: when the sender shouldn't
know about the receiver.

### Common Mistakes

- **Connecting to a lambda that captures `this`** and the object gets
  deleted → crash. Pass a context object:
  ```cpp
  connect(sender, &Sender::sig, this, [this]{ /* safe */ });
  ```
  When `this` is destroyed, the connection is auto-disconnected.
- **Signal-slot chains (A→B→C→D)** — long chains are hard to debug.
  If the chain is longer than 2, consider direct calls or restructuring.
- **`QObject::sender()`** — fragile. Different signals connected to the
  same slot produce different `sender()` results. Avoid relying on it.

## Thread Safety Rules

- **QWidget is not thread-safe.** All widget operations must happen on the
  main (GUI) thread. **No exceptions.**
- Use `QMetaObject::invokeMethod(obj, "slot", Qt::QueuedConnection)` or
  signals with queued connections to communicate from worker → GUI.
- `QObject` affinity: an object "lives" in the thread where it was created.
  Move it with `moveToThread()`, but only if it has no parent.

## Event Loop Pitfalls

- **Blocking the event loop** (long computation, synchronous I/O) freezes
  the UI. Move heavy work to a `QThread` or use `QtConcurrent`.
- **Nested event loops** (`QDialog::exec()`, `QEventLoop`) are fragile.
  They re-enter the event loop and can cause unexpected re-entrancy in
  slots. Prefer `QDialog::open()` with a signal for the result.
- **Timer precision**: `QTimer` with 0ms interval fires on every event loop
  iteration — use sparingly. For frame-rate work, use `QElapsedTimer` and
  `startTimer()`.

## Widget Lifecycle

1. Constructor: set up child widgets, layouts, connections.
2. `showEvent()`: widget is about to become visible.
3. `paintEvent()`: draw content (for custom widgets only).
4. `resizeEvent()`: geometry changed.
5. `hideEvent()`: widget hidden.
6. `closeEvent()`: user requested close (override to confirm).
7. Destructor: children auto-deleted by Qt tree.

**Never** do heavy I/O or blocking work in `paintEvent()`.

## QSS (Qt Style Sheets) Rules

- Object names must match between C++ (`setObjectName()`) and QSS selectors.
- Dynamic properties (`setProperty("state", "active")`) are powerful for
  state-based styling but require `style()->unpolish(this); style()->polish(this);`
  or `update()` to re-apply styles after property changes.
- QSS is evaluated per-widget. Deep inheritance + complex selectors = slow.
  Keep selectors simple: `#objectName`, `.ClassName`, `QWidget[state="x"]`.

### QSS vs QStyle: When to Use Which

- **QSS**: good for colors, fonts, borders, padding, background images.
  Fast iteration. Sufficient for most AIO Server surfaces.
- **QStyle subclass**: needed when QSS can't express the effect (custom
  painting, pixel-level control, animation-driven properties). KDAB
  recommends QStyle for production apps that need full visual control.
- **AIO Server approach**: QSS for static styling, `paintEvent()` overrides
  for custom rendering (shadows, gradients, complex focus states),
  `QPropertyAnimation` for all motion.

## Property Animations

`QPropertyAnimation` is the primary tool for making the UI feel alive.
Every state change (focus, unfocus, press, page transition) should animate.

### Basic Focus Animation

```cpp
// In a custom widget (e.g., HomeTile)
void HomeTile::setFocused(bool focused) {
    auto* anim = new QPropertyAnimation(this, "geometry");
    anim->setDuration(focused ? 200 : 150);
    anim->setEasingCurve(focused ? QEasingCurve::OutCubic
                                 : QEasingCurve::OutQuad);
    QRect target = focused ? expandedRect() : normalRect();
    anim->setEndValue(target);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}
```

### Custom Animated Properties

Expose a Q_PROPERTY for any value you want to animate:

```cpp
class FocusCard : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal focusScale READ focusScale WRITE setFocusScale)
    Q_PROPERTY(qreal shadowRadius READ shadowRadius WRITE setShadowRadius)
public:
    qreal focusScale() const { return m_focusScale; }
    void setFocusScale(qreal s) { m_focusScale = s; update(); }
    // ... same for shadowRadius
protected:
    void paintEvent(QPaintEvent*) override {
        // Use m_focusScale and m_shadowRadius in paint code
    }
private:
    qreal m_focusScale = 1.0;
    qreal m_shadowRadius = 0.0;
};
```

Then animate with:

```cpp
auto* scaleAnim = new QPropertyAnimation(card, "focusScale");
scaleAnim->setDuration(200);
scaleAnim->setStartValue(1.0);
scaleAnim->setEndValue(1.06); // 6% scale-up
```

### Parallel Animation Groups

Combine multiple property animations for compound focus effects:

```cpp
auto* group = new QParallelAnimationGroup(this);
group->addAnimation(scaleAnim);    // scale 1.0 → 1.06
group->addAnimation(shadowAnim);   // shadow 0 → 12px
group->addAnimation(opacityAnim);  // title opacity 0.7 → 1.0
group->start(QAbstractAnimation::DeleteWhenStopped);
```

### Animation Timing Reference

| Transition   | Duration  | Easing   | Notes                  |
| ------------ | --------- | -------- | ---------------------- |
| Focus in     | 200ms     | OutCubic | Fast response to input |
| Focus out    | 150ms     | OutQuad  | Faster exit            |
| Press dip    | 80-120ms  | OutQuad  | Immediate tactile feel |
| Release      | 150ms     | OutCubic | Bounce back            |
| Page enter   | 250-300ms | OutCubic | Slide/fade in          |
| Page exit    | 200ms     | OutQuad  | Faster than enter      |
| Shelf scroll | 250ms     | OutCubic | Card-snapping scroll   |

**Key rule**: exit is always faster than enter. The user should never wait
for an animation to finish before seeing the next state.

### Animation Anti-Patterns

- **Don't block input during animations.** The user should be able to
  navigate away mid-animation. Always check for running animations and
  stop/replace them if a new input arrives.
- **Don't animate on the wrong thread.** All `QPropertyAnimation` must run
  on the GUI thread.
- **Don't create animation objects in hot loops.** Reuse or pool them for
  scroll-heavy UIs.
- **Don't use `QGraphicsEffect` for per-card shadows.** Each effect does a
  full offscreen render. Paint shadows manually in `paintEvent()` instead.

## Retrieval Hints

- Load when working on GUI code, debugging widget crashes, reviewing
  signal-slot connections, or implementing animations.
- Cross-reference with `tv-ui-design-patterns.md` for focus/animation specs.
- Key terms: QObject, parent ownership, deleteLater, signal slot, queued
  connection, thread affinity, event loop, QSS, QStyle, QPropertyAnimation,
  widget lifecycle, focus animation, easing curve.

## Verification Basis

Synthesized from: Qt 6.11 official documentation (Signals & Slots, Object
Trees & Ownership, Threads-QObject), Qt forum expert threads, Qt wiki
Threads/Events/QObjects article, and AIO Server GUI codebase patterns.
