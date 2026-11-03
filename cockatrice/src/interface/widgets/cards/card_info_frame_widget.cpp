#include "card_info_frame_widget.h"

#include "../../../client/settings/cache_settings.h"
#include "../../../game_graphics/board/abstract_card_item.h"
#include "card_info_picture_widget.h"
#include "card_info_text_widget.h"
#include "libcockatrice/card/card_info.h"
#include "libcockatrice/card/database/card_database_querier.h"
#include "libcockatrice/card/printing/exact_card.h"

#include <QList>
#include <QObject>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>
#include <QtGlobal>
#include <libcockatrice/card/database/card_database_manager.h>
#include <libcockatrice/card/relation/card_relation.h>
#include <libcockatrice/settings/cards_display_settings.h>
#include <libcockatrice/utility/card_ref.h>
#include <libcockatrice/utility/qt_utils.h>
#include <qnamespace.h>

struct CardRef;

namespace
{
constexpr int CARD_INFO_SPLITTER_POSITION_SCALE = 10000;
constexpr int TRANSFORMATION_BUTTON_WEIGHT = 800;
}

CardInfoFrameWidget::CardInfoFrameWidget(QWidget *parent)
    : QTabWidget(parent), viewTransformationButton(nullptr), cardTextOnly(false)
{
    setContentsMargins(3, 3, 3, 3);
    pic = new CardInfoPictureWidget();
    pic->setObjectName("pic");
    connect(pic, &CardInfoPictureWidget::cardChanged, this,
            qOverload<const ExactCard &>(&CardInfoFrameWidget::setCard));

    text = new CardInfoTextWidget();
    text->setObjectName("text");
    connect(text, &CardInfoTextWidget::linkActivated, this, qOverload<const QString &>(&CardInfoFrameWidget::setCard));

    tab1 = new QWidget(this);
    tab2 = new QWidget(this);
    tab3 = new QWidget(this);

    tab1->setObjectName("tab1");
    tab2->setObjectName("tab2");
    tab3->setObjectName("tab3");

    insertTab(ImageOnlyView, tab1, QString());
    insertTab(TextOnlyView, tab2, QString());
    insertTab(ImageAndTextView, tab3, QString());
    connect(this, &CardInfoFrameWidget::currentChanged, this, &CardInfoFrameWidget::setViewMode);

    tab1Layout = QtUtils::makeLayout<QVBoxLayout>();
    tab1Layout->setObjectName("tab1Layout");
    tab1Layout->setSpacing(0);
    tab1->setLayout(tab1Layout);

    tab2Layout = QtUtils::makeLayout<QVBoxLayout>();
    tab2Layout->setObjectName("tab2Layout");
    tab2Layout->setSpacing(0);
    tab2->setLayout(tab2Layout);

    splitter = new QSplitter();
    splitter->setObjectName("splitter");
    splitter->setOrientation(Qt::Vertical);
    connect(splitter, &QSplitter::splitterMoved, this, [this] { updateSplitterState(); });

    tab3Layout = QtUtils::makeLayout<QVBoxLayout>();
    tab3Layout->setObjectName("tab3Layout");
    tab3Layout->setSpacing(0);
    tab3Layout->addWidget(splitter);
    tab3->setLayout(tab3Layout);

    setViewMode(SettingsCache::instance().cardsDisplay().getCardInfoViewMode());
}

void CardInfoFrameWidget::retranslateUi()
{
    setTabText(ImageOnlyView, tr("Image"));
    setTabText(TextOnlyView, tr("Description"));
    setTabText(ImageAndTextView, tr("Both"));

    if (viewTransformationButton) {
        viewTransformationButton->setText(tr("View transformation"));
    }
}

void CardInfoFrameWidget::setViewTransformationButtonVisibility(bool visible)
{
    if (!viewTransformationButton && visible) {
        viewTransformationButton = new QPushButton();
        viewTransformationButton->setObjectName("viewTransformationButton");
        connect(viewTransformationButton, &QPushButton::clicked, this, &CardInfoFrameWidget::viewTransformation);
        refreshLayout();
    } else if (viewTransformationButton && !visible) {
        // Deleting a widget automatically removes it from its parent
        viewTransformationButton->deleteLater();
        viewTransformationButton = nullptr;
    }
}

/**
 * Adds the widgets to the layouts that are relevant to the currently active tab.
 *
 * QWidgets can only have one parent, so we need to re-parent the shared widgets whenever we switch tabs.
 */
void CardInfoFrameWidget::refreshLayout()
{
    if (currentIndex() != ImageAndTextView) {
        updateSplitterState();
    }

    switch (currentIndex()) {
        case ImageOnlyView:
        case TextOnlyView:
            // We need to always parent all widgets, even the ones that aren't visible,
            // since an unparented widget becomes free-floating.
            tab1Layout->addWidget(pic);
            if (viewTransformationButton) {
                tab1Layout->addWidget(viewTransformationButton);
            }
            tab2Layout->addWidget(text);
            break;
        case ImageAndTextView:
            splitter->addWidget(pic);
            if (viewTransformationButton) {
                splitter->addWidget(viewTransformationButton);
            }
            splitter->addWidget(text);
            applySplitterState();
            break;
        default:
            break;
    }
    retranslateUi();
}

QByteArray CardInfoFrameWidget::getSplitterState() const
{
    if (splitter->count() > 1) {
        const QList<int> sizes = splitter->sizes();
        const int pictureIndex = splitter->indexOf(pic);
        const int textIndex = splitter->indexOf(text);
        if (pictureIndex >= 0 && textIndex >= 0) {
            const int pictureSize = sizes.at(pictureIndex);
            const int textSize = sizes.at(textIndex);
            const int totalSize = pictureSize + textSize;
            if (totalSize > 0) {
                return QByteArray::number(pictureSize * CARD_INFO_SPLITTER_POSITION_SCALE / totalSize);
            }
        }
    }
    return splitterState;
}

void CardInfoFrameWidget::setSplitterState(const QByteArray &state)
{
    bool valid = false;
    const int position = state.toInt(&valid);
    if (!valid || position < 0 || position > CARD_INFO_SPLITTER_POSITION_SCALE) {
        return;
    }

    splitterState = state;
    applySplitterState();
}

void CardInfoFrameWidget::resetSplitterState()
{
    splitterState.clear();
    if (splitter->count() > 1) {
        QList<int> sizes;
        for (int i = 0; i < splitter->count(); ++i) {
            sizes.append(1);
        }
        splitter->setSizes(sizes);
    }
}

void CardInfoFrameWidget::updateSplitterState()
{
    const QByteArray state = getSplitterState();
    if (!state.isEmpty()) {
        splitterState = state;
    }
}

void CardInfoFrameWidget::applySplitterState()
{
    if (splitterState.isEmpty() || splitter->count() < 2) {
        return;
    }

    bool valid = false;
    const int position = splitterState.toInt(&valid);
    if (!valid || position < 0 || position > CARD_INFO_SPLITTER_POSITION_SCALE) {
        return;
    }

    if (splitter->count() == 2) {
        QList<int> sizes{0, 0};
        sizes[splitter->indexOf(pic)] = position;
        sizes[splitter->indexOf(text)] = CARD_INFO_SPLITTER_POSITION_SCALE - position;
        splitter->setSizes(sizes);
    } else if (splitter->count() == 3) {
        // The optional button gets a small share without changing the saved image/text ratio.
        QList<int> sizes{TRANSFORMATION_BUTTON_WEIGHT, TRANSFORMATION_BUTTON_WEIGHT, TRANSFORMATION_BUTTON_WEIGHT};
        sizes[splitter->indexOf(pic)] = position;
        sizes[splitter->indexOf(text)] = CARD_INFO_SPLITTER_POSITION_SCALE - position;
        splitter->setSizes(sizes);
    }
}

void CardInfoFrameWidget::setViewMode(int mode)
{
    if (currentIndex() != mode) {
        setCurrentIndex(mode);
    }

    refreshLayout();

    SettingsCache::instance().cardsDisplay().setCardInfoViewMode(mode);
}

static bool hasTransformation(const CardInfo &info)
{
    for (const auto &cardRelation : info.getAllRelatedCards()) {
        if (cardRelation->getDoesTransform()) {
            return true;
        }
    }
    return false;
}

void CardInfoFrameWidget::setCard(const ExactCard &card)
{
    if (exactCard) {
        disconnect(exactCard.getCardPtr().data(), nullptr, this, nullptr);
    }

    exactCard = card;

    if (exactCard) {
        connect(exactCard.getCardPtr().data(), &QObject::destroyed, this, &CardInfoFrameWidget::clearCard);
    }

    setViewTransformationButtonVisibility(hasTransformation(exactCard.getInfo()));

    text->setCard(exactCard);
    pic->setCard(exactCard);
}

void CardInfoFrameWidget::setCard(const QString &cardName)
{
    setCard(CardDatabaseManager::query()->guessCard({cardName}));
}

void CardInfoFrameWidget::setCard(const CardRef &cardRef)
{
    setCard(CardDatabaseManager::query()->getCard(cardRef));
}

void CardInfoFrameWidget::setCard(AbstractCardItem *card)
{
    if (card) {
        setCard(card->getCard());
    }
}

void CardInfoFrameWidget::viewTransformation()
{
    if (exactCard) {
        const auto &cardRelations = exactCard.getInfo().getAllRelatedCards();
        for (const auto &cardRelation : cardRelations) {
            if (cardRelation->getDoesTransform()) {
                setCard(cardRelation->getName());
                break;
            }
        }
    }
}

void CardInfoFrameWidget::clearCard()
{
    setCard(ExactCard());
}
