#include "rolo/ui/Context.hpp"
#include "rolo/ui/widgets/Basic.hpp"
#include "rolo/ui/widgets/Inspector.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace rolo::ui;

namespace {

struct InspectorHarness {
    Context ctx;
    Section* section = nullptr;
    PropertyField* field = nullptr;
    ValueBox* x = nullptr;
    ValueBox* y = nullptr;
    int helpClicks = 0;

    InspectorHarness()
    {
        auto root = std::make_unique<Column>(0.0f);
        section = &root->emplace<Section>("Transform");
        section->setHelpAction([this] { ++helpClicks; });
        field = &section->body().emplace<PropertyField>("Position");
        x = &field->addValue(std::make_unique<ValueBox>("640.0"));
        x->setSuffix("X");
        y = &field->addValue(std::make_unique<ValueBox>("360.0"));
        y->setSuffix("Y");
        root->emplace<Spacer>();
        ctx.setRoot(std::move(root));
        ctx.setViewport({400, 300}, 1.0f);
        ctx.render();
    }

    void click(Vec2 p)
    {
        ctx.pointerMove(p);
        ctx.pointerButton(MouseButton::Left, true, {});
        ctx.pointerButton(MouseButton::Left, false, {});
        ctx.render();
    }
};

} // namespace

TEST_CASE("Property values sit right of the label column and share its width", "[ui][inspector]")
{
    InspectorHarness h;
    const Rect row = h.field->bounds();
    const float labelColumn = row.x + PropertyField::labelWidth(row.width);
    CHECK(h.x->bounds().x == labelColumn);
    CHECK(h.y->bounds().x > h.x->bounds().right());
    CHECK(h.y->bounds().right() <= row.right() + 0.5f);
    CHECK(std::abs(h.x->bounds().width - h.y->bounds().width) <= 1.0f);
}

TEST_CASE("Clicking a section header folds and unfolds it", "[ui][inspector]")
{
    InspectorHarness h;
    const float open = h.section->bounds().height;
    h.click({60.0f, h.section->bounds().y + 10.0f});
    CHECK_FALSE(h.section->isExpanded());
    CHECK(h.section->bounds().height < open);
    CHECK_FALSE(h.section->body().isVisible());
    h.click({60.0f, h.section->bounds().y + 10.0f});
    CHECK(h.section->isExpanded());
    CHECK(h.section->body().isVisible());
}

TEST_CASE("A section's help button runs its action without folding it", "[ui][inspector]")
{
    InspectorHarness h;
    const Rect help = h.section->helpButton();
    REQUIRE_FALSE(help.isEmpty());
    CHECK(h.section->moreButton().isEmpty()); // no action, no button
    h.click(help.center());
    CHECK(h.helpClicks == 1);
    CHECK(h.section->isExpanded());
}

TEST_CASE("Tick-box values have a fixed size; text values stretch", "[ui][inspector]")
{
    Context ctx;
    ValueBox tick("", ValueBox::Kind::Check);
    ValueBox text("DEFAULT", ValueBox::Kind::Choice);
    CHECK(tick.stretch() == 0.0f);
    CHECK(text.stretch() > 0.0f);
    CHECK(tick.measure(ctx, {300, 300}).width < text.measure(ctx, {300, 300}).width);
}

TEST_CASE("An empty state paints its message within the panel", "[ui][inspector]")
{
    Context ctx;
    auto root = std::make_unique<Column>(0.0f);
    root->emplace<EmptyState>(Icon::Folder, "No project open",
                              "Projects and their assets arrive with Milestone 3, when scenes can be saved.");
    ctx.setRoot(std::move(root));
    ctx.setViewport({220, 400}, 1.0f);
    const DrawList& draw = ctx.render();
    CHECK_FALSE(draw.commands().empty());
    for (const DrawCommand& cmd : draw.commands()) {
        CHECK(cmd.clip.right() <= 220.0f);
    }
}

TEST_CASE("A marked property row's label can be clicked; an unmarked one's cannot", "[ui][inspector]")
{
    InspectorHarness h;
    int clicks = 0;
    h.field->setOnLabelClick([&clicks] { ++clicks; });
    const Rect label = h.field->labelRect();
    const Vec2 onLabel{label.x + 20.0f, label.y + label.height * 0.5f};

    h.click(onLabel);
    CHECK(clicks == 0);   // not marked: the label is only a label

    h.field->setMarked(true);
    h.ctx.render();
    h.click(onLabel);
    CHECK(clicks == 1);
    // A click on the value is the value's, not the label's.
    const Rect value = h.x->bounds();
    h.click({value.x + value.width * 0.5f, value.y + value.height * 0.5f});
    CHECK(clicks == 1);
    // Pressed on the label and let go elsewhere: no click.
    h.ctx.pointerMove(onLabel);
    h.ctx.pointerButton(MouseButton::Left, true, {});
    h.ctx.pointerMove({onLabel.x, onLabel.y + 200.0f});
    h.ctx.pointerButton(MouseButton::Left, false, {});
    CHECK(clicks == 1);
}
