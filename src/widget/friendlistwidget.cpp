/*
    Copyright © 2014-2015 by The qTox Project Contributors

    This file is part of qTox, a Qt-based graphical interface for Tox.

    qTox is libre software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    qTox is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with qTox.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "friendlistwidget.h"
#include "circlewidget.h"
#include "friendlistlayout.h"
#include "friendwidget.h"
#include "groupwidget.h"
#include "widget.h"
#include "src/friend.h"
#include "src/friendlist.h"
#include "src/persistence/settings.h"
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QGridLayout>
#include <QMimeData>
#include <QTimer>
#include <cassert>

enum class Time
{
    Today,
    Yesterday,
    ThisWeek,
    ThisMonth,
    Month1Ago,
    Month2Ago,
    Month3Ago,
    Month4Ago,
    Month5Ago,
    LongAgo,
    Never
};

static const int LAST_TIME = static_cast<int>(Time::Never);

Time getTime(const QDate& date)
{
    if (date == QDate()) {
        return Time::Never;
    }

    QDate today = QDate::currentDate();
    // clang-format off
    const QMap<Time, QDate> dates {
        { Time::Today,     today.addDays(0)    },
        { Time::Yesterday, today.addDays(-1)   },
        { Time::ThisWeek,  today.addDays(-6)   },
        { Time::ThisMonth, today.addMonths(-1) },
        { Time::Month1Ago, today.addMonths(-2) },
        { Time::Month2Ago, today.addMonths(-3) },
        { Time::Month3Ago, today.addMonths(-4) },
        { Time::Month4Ago, today.addMonths(-5) },
        { Time::Month5Ago, today.addMonths(-6) },
    };
    // clang-format on

    for (Time time : dates.keys()) {
        if (dates[time] <= date) {
            return time;
        }
    }

    return Time::LongAgo;
}

QDate getDateFriend(const Friend* contact)
{
    return Settings::getInstance().getFriendActivity(contact->getPublicKey());
}

qint64 timeUntilTomorrow()
{
    QDateTime now = QDateTime::currentDateTime();
    QDateTime tomorrow = now.addDays(1); // Tomorrow.
    tomorrow.setTime(QTime());           // Midnight.
    return now.msecsTo(tomorrow);
}

FriendListWidget::FriendListWidget(Widget* parent, bool groupsOnTop)
    : QWidget(parent)
    // Prevent Valgrind from complaining. We're changing this to Name here.
    // Must be Activity for change to take effect.
    , mode(Activity)
    , groupsOnTop(groupsOnTop)
{
    listLayout = new FriendListLayout();
    setLayout(listLayout);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

    groupLayout.getLayout()->setSpacing(0);
    groupLayout.getLayout()->setMargin(0);

    // Prevent QLayout's add child warning before setting the mode.
    listLayout->removeItem(listLayout->getLayoutOnline());
    listLayout->removeItem(listLayout->getLayoutOffline());

    setMode(Name);

    onGroupchatPositionChanged(groupsOnTop);
    dayTimer = new QTimer(this);
    dayTimer->setTimerType(Qt::VeryCoarseTimer);
    connect(dayTimer, &QTimer::timeout, this, &FriendListWidget::dayTimeout);
    dayTimer->start(timeUntilTomorrow());

    setAcceptDrops(true);
}

FriendListWidget::~FriendListWidget()
{
    if (activityLayout != nullptr) {
        QLayoutItem* item;
        while ((item = activityLayout->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }
        delete activityLayout;
    }

    if (circleLayout != nullptr) {
        QLayoutItem* item;
        while ((item = circleLayout->getLayout()->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }
        delete circleLayout;
    }
}

void FriendListWidget::setMode(Mode mode)
{
    if (this->mode == mode)
        return;

    this->mode = mode;

    if (mode == Name) {
        circleLayout = new GenericChatItemLayout;
        circleLayout->getLayout()->setSpacing(0);
        circleLayout->getLayout()->setMargin(0);

        for (int i = 0; i < Settings::getInstance().getCircleCount(); ++i) {
            addCircleWidget(i);
            CircleWidget::getFromID(i)->setVisible(false);
        }

        // Only display circles once all created to avoid artifacts.
        for (int i = 0; i < Settings::getInstance().getCircleCount(); ++i)
            CircleWidget::getFromID(i)->setVisible(true);

        int count = activityLayout ? activityLayout->count() : 0;
        for (int i = 0; i < count; i++) {
            QWidget* widget = activityLayout->itemAt(i)->widget();
            CategoryWidget* categoryWidget = qobject_cast<CategoryWidget*>(widget);
            if (categoryWidget) {
                categoryWidget->moveFriendWidgets(this);
            } else {
                qWarning() << "Unexpected widget";
            }
        }

        listLayout->addLayout(listLayout->getLayoutOnline());
        listLayout->addLayout(listLayout->getLayoutOffline());
        listLayout->addLayout(circleLayout->getLayout());
        onGroupchatPositionChanged(groupsOnTop);

        if (activityLayout != nullptr) {
            QLayoutItem* item;
            while ((item = activityLayout->takeAt(0)) != nullptr) {
                delete item->widget();
                delete item;
            }
            delete activityLayout;
            activityLayout = nullptr;
        }

        reDraw();
    } else if (mode == Activity) {
        QLocale ql(Settings::getInstance().getTranslation());
        QDate today = QDate::currentDate();
#define COMMENT "Category for sorting friends by activity"
        // clang-format off
        const QMap<Time, QString> names {
            { Time::Today,     tr("Today",                      COMMENT) },
            { Time::Yesterday, tr("Yesterday",                  COMMENT) },
            { Time::ThisWeek,  tr("Last 7 days",                COMMENT) },
            { Time::ThisMonth, tr("This month",                 COMMENT) },
            { Time::LongAgo,   tr("Older than 6 Months",        COMMENT) },
            { Time::Never,     tr("Never",                      COMMENT) },
            { Time::Month1Ago, ql.monthName(today.addMonths(-1).month()) },
            { Time::Month2Ago, ql.monthName(today.addMonths(-2).month()) },
            { Time::Month3Ago, ql.monthName(today.addMonths(-3).month()) },
            { Time::Month4Ago, ql.monthName(today.addMonths(-4).month()) },
            { Time::Month5Ago, ql.monthName(today.addMonths(-5).month()) },
        };
// clang-format on
#undef COMMENT

        activityLayout = new QVBoxLayout();
        bool compact = Settings::getInstance().getCompactLayout();
        for (Time t : names.keys()) {
            CategoryWidget* category = new CategoryWidget(compact, this);
            category->setName(names[t]);
            activityLayout->addWidget(category);
        }

        moveFriends(listLayout->getLayoutOffline());
        moveFriends(listLayout->getLayoutOnline());
        moveFriends(circleLayout->getLayout());

        for (int i = 0; i < activityLayout->count(); ++i) {
            QWidget* widget = activityLayout->itemAt(i)->widget();
            CategoryWidget* categoryWidget = qobject_cast<CategoryWidget*>(widget);
            categoryWidget->setVisible(categoryWidget->hasChatrooms());
        }

        listLayout->removeItem(listLayout->getLayoutOnline());
        listLayout->removeItem(listLayout->getLayoutOffline());

        if (circleLayout != nullptr) {
            listLayout->removeItem(circleLayout->getLayout());

            QLayoutItem* item;
            while ((item = circleLayout->getLayout()->takeAt(0)) != nullptr) {
                delete item->widget();
                delete item;
            }
            delete circleLayout;
            circleLayout = nullptr;
        }

        listLayout->insertLayout(1, activityLayout);

        reDraw();
    }
}

void FriendListWidget::moveFriends(QLayout* layout)
{
    for (int i = 0; i < layout->count(); i++) {
        QWidget* widget = layout->itemAt(i)->widget();
        FriendWidget* friendWidget = qobject_cast<FriendWidget*>(widget);
        CircleWidget* circleWidget = qobject_cast<CircleWidget*>(widget);
        if (circleWidget) {
            circleWidget->moveFriendWidgets(this);
        } else if (friendWidget) {
            const Friend* contact = friendWidget->getFriend();
            QDate activityDate = getDateFriend(contact);
            int time = static_cast<int>(getTime(activityDate));

            QWidget* w = activityLayout->itemAt(time)->widget();
            CategoryWidget* categoryWidget = qobject_cast<CategoryWidget*>(w);
            categoryWidget->addFriendWidget(friendWidget, contact->getStatus());
        }
    }
}

FriendListWidget::Mode FriendListWidget::getMode() const
{
    return mode;
}

void FriendListWidget::addGroupWidget(GroupWidget* widget)
{
    groupLayout.addSortedWidget(widget);
    connect(widget, &GroupWidget::renameRequested, this, &FriendListWidget::renameGroupWidget);
}

void FriendListWidget::addFriendWidget(FriendWidget* w, Status s, int circleIndex)
{
    CircleWidget* circleWidget = CircleWidget::getFromID(circleIndex);
    if (circleWidget == nullptr)
        moveWidget(w, s, true);
    else
        circleWidget->addFriendWidget(w, s);
}

void FriendListWidget::removeFriendWidget(FriendWidget* w)
{
    const Friend* contact = w->getFriend();
    if (mode == Activity) {
        QDate activityDate = getDateFriend(contact);
        int time = static_cast<int>(getTime(activityDate));
        QWidget* widget = activityLayout->itemAt(time)->widget();
        CategoryWidget* categoryWidget = qobject_cast<CategoryWidget*>(widget);
        categoryWidget->removeFriendWidget(w, contact->getStatus());
        categoryWidget->setVisible(categoryWidget->hasChatrooms());
    } else {
        int id = Settings::getInstance().getFriendCircleID(contact->getPublicKey());
        CircleWidget* circleWidget = CircleWidget::getFromID(id);
        if (circleWidget != nullptr) {
            circleWidget->removeFriendWidget(w, contact->getStatus());
            Widget::getInstance()->searchCircle(circleWidget);
        }
    }
}

void FriendListWidget::addCircleWidget(int id)
{
    createCircleWidget(id);
}

void FriendListWidget::addCircleWidget(FriendWidget* friendWidget)
{
    CircleWidget* circleWidget = createCircleWidget();
    if (circleWidget != nullptr) {
        if (friendWidget != nullptr) {
            const Friend* f = friendWidget->getFriend();
            ToxPk toxPk = f->getPublicKey();
            int circleId = Settings::getInstance().getFriendCircleID(toxPk);
            CircleWidget* circleOriginal = CircleWidget::getFromID(circleId);

            circleWidget->addFriendWidget(friendWidget, f->getStatus());
            circleWidget->setExpanded(true);

            if (circleOriginal != nullptr)
                Widget::getInstance()->searchCircle(circleOriginal);
        }

        Widget::getInstance()->searchCircle(circleWidget);

        if (window()->isActiveWindow())
            circleWidget->editName();
    }
    reDraw();
}

void FriendListWidget::removeCircleWidget(CircleWidget* widget)
{
    circleLayout->removeSortedWidget(widget);
    widget->deleteLater();
}

void FriendListWidget::searchChatrooms(const QString& searchString, bool hideOnline,
                                       bool hideOffline, bool hideGroups)
{
    groupLayout.search(searchString, hideGroups);
    listLayout->searchChatrooms(searchString, hideOnline, hideOffline);

    if (circleLayout != nullptr) {
        for (int i = 0; i != circleLayout->getLayout()->count(); ++i) {
            CircleWidget* circleWidget =
                static_cast<CircleWidget*>(circleLayout->getLayout()->itemAt(i)->widget());
            circleWidget->search(searchString, true, hideOnline, hideOffline);
        }
    } else if (activityLayout != nullptr) {
        for (int i = 0; i != activityLayout->count(); ++i) {
            CategoryWidget* categoryWidget =
                static_cast<CategoryWidget*>(activityLayout->itemAt(i)->widget());
            categoryWidget->search(searchString, true, hideOnline, hideOffline);
            categoryWidget->setVisible(categoryWidget->hasChatrooms());
        }
    }
}

void FriendListWidget::renameGroupWidget(GroupWidget* groupWidget, const QString& newName)
{
    groupLayout.removeSortedWidget(groupWidget);
    groupWidget->setName(newName);
    groupLayout.addSortedWidget(groupWidget);
}

void FriendListWidget::renameCircleWidget(CircleWidget* circleWidget, const QString& newName)
{
    circleLayout->removeSortedWidget(circleWidget);
    circleWidget->setName(newName);
    circleLayout->addSortedWidget(circleWidget);
}

void FriendListWidget::onGroupchatPositionChanged(bool top)
{
    groupsOnTop = top;

    if (mode != Name)
        return;

    listLayout->removeItem(groupLayout.getLayout());

    if (top)
        listLayout->insertLayout(0, groupLayout.getLayout());
    else
        listLayout->insertLayout(1, groupLayout.getLayout());

    reDraw();
}

void FriendListWidget::cycleContacts(GenericChatroomWidget* activeChatroomWidget, bool forward)
{
    if (!activeChatroomWidget) {
        return;
    }

    int index = -1;
    FriendWidget* friendWidget = qobject_cast<FriendWidget*>(activeChatroomWidget);

    if (mode == Activity) {
        if (!friendWidget) {
            return;
        }

        QDate activityDate = getDateFriend(friendWidget->getFriend());
        index = static_cast<int>(getTime(activityDate));
        QWidget* widget = activityLayout->itemAt(index)->widget();
        CategoryWidget* categoryWidget = qobject_cast<CategoryWidget*>(widget);

        if (categoryWidget == nullptr || categoryWidget->cycleContacts(friendWidget, forward)) {
            return;
        }

        index += forward ? 1 : -1;

        for (;;) {
            // Bounds checking.
            if (index < 0) {
                index = LAST_TIME;
                continue;
            } else if (index > LAST_TIME) {
                index = 0;
                continue;
            }

            widget = activityLayout->itemAt(index)->widget();
            categoryWidget = qobject_cast<CategoryWidget*>(widget);

            if (categoryWidget != nullptr) {
                if (!categoryWidget->cycleContacts(forward)) {
                    // Skip empty or finished categories.
                    index += forward ? 1 : -1;
                    continue;
                }
            }

            break;
        }

        return;
    }

    QLayout* currentLayout = nullptr;
    CircleWidget* circleWidget = nullptr;

    if (friendWidget != nullptr) {
        const ToxPk& pk = friendWidget->getFriend()->getPublicKey();
        uint32_t circleId = Settings::getInstance().getFriendCircleID(pk);
        circleWidget = CircleWidget::getFromID(circleId);
        if (circleWidget != nullptr) {
            if (circleWidget->cycleContacts(friendWidget, forward)) {
                return;
            }

            index = circleLayout->indexOfSortedWidget(circleWidget);
            currentLayout = circleLayout->getLayout();
        } else {
            currentLayout = listLayout->getLayoutOnline();
            index = listLayout->indexOfFriendWidget(friendWidget, true);
            if (index == -1) {
                currentLayout = listLayout->getLayoutOffline();
                index = listLayout->indexOfFriendWidget(friendWidget, false);
            }
        }
    } else {
        GroupWidget* groupWidget = qobject_cast<GroupWidget*>(activeChatroomWidget);
        if (groupWidget != nullptr) {
            currentLayout = groupLayout.getLayout();
            index = groupLayout.indexOfSortedWidget(groupWidget);
        } else {
            return;
        };
    }

    index += forward ? 1 : -1;

    for (;;) {
        // Bounds checking.
        if (index < 0) {
            currentLayout = nextLayout(currentLayout, forward);
            index = currentLayout->count() - 1;
            continue;
        } else if (index >= currentLayout->count()) {
            currentLayout = nextLayout(currentLayout, forward);
            index = 0;
            continue;
        }

        // Go to the actual next index.
        if (currentLayout == listLayout->getLayoutOnline()
            || currentLayout == listLayout->getLayoutOffline()
            || currentLayout == groupLayout.getLayout()) {
            GenericChatroomWidget* chatWidget =
                qobject_cast<GenericChatroomWidget*>(currentLayout->itemAt(index)->widget());

            if (chatWidget != nullptr)
                emit chatWidget->chatroomWidgetClicked(chatWidget);

            return;
        } else if (currentLayout == circleLayout->getLayout()) {
            circleWidget = qobject_cast<CircleWidget*>(currentLayout->itemAt(index)->widget());
            if (circleWidget != nullptr) {
                if (!circleWidget->cycleContacts(forward)) {
                    // Skip empty or finished circles.
                    index += forward ? 1 : -1;
                    continue;
                }
            }
            return;
        } else {
            return;
        }
    }
}

void FriendListWidget::dragEnterEvent(QDragEnterEvent* event)
{
    ToxId toxId(event->mimeData()->text());
    Friend* frnd = FriendList::findFriend(toxId.getPublicKey());
    if (frnd)
        event->acceptProposedAction();
}

void FriendListWidget::dropEvent(QDropEvent* event)
{
    // Check, that the element is dropped from qTox
    QObject* o = event->source();
    FriendWidget* widget = qobject_cast<FriendWidget*>(o);
    if (!widget)
        return;

    // Check, that the user has a friend with the same ToxId
    ToxId toxId(event->mimeData()->text());
    Friend* f = FriendList::findFriend(toxId.getPublicKey());
    if (!f)
        return;

    // Save CircleWidget before changing the Id
    int circleId = Settings::getInstance().getFriendCircleID(f->getPublicKey());
    CircleWidget* circleWidget = CircleWidget::getFromID(circleId);

    moveWidget(widget, f->getStatus(), true);

    if (circleWidget)
        circleWidget->updateStatus();
}

void FriendListWidget::dayTimeout()
{
    if (mode == Activity) {
        setMode(Name);
        setMode(Activity); // Refresh all.
    }

    dayTimer->start(timeUntilTomorrow());
}

void FriendListWidget::moveWidget(FriendWidget* widget, Status s, bool add)
{
    if (mode == Name) {
        const Friend* f = widget->getFriend();
        int circleId = Settings::getInstance().getFriendCircleID(f->getPublicKey());
        CircleWidget* circleWidget = CircleWidget::getFromID(circleId);

        if (circleWidget == nullptr || add) {
            if (circleId != -1)
                Settings::getInstance().setFriendCircleID(f->getPublicKey(), -1);

            listLayout->addFriendWidget(widget, s);
            return;
        }

        circleWidget->addFriendWidget(widget, s);
    } else {
        const Friend* contact = widget->getFriend();
        QDate activityDate = getDateFriend(contact);
        int time = static_cast<int>(getTime(activityDate));
        QWidget* w = activityLayout->itemAt(time)->widget();
        CategoryWidget* categoryWidget = qobject_cast<CategoryWidget*>(w);
        categoryWidget->addFriendWidget(widget, contact->getStatus());
        categoryWidget->show();
    }
}

void FriendListWidget::updateActivityDate(const QDate& date)
{
    if (mode != Activity)
        return;

    int time = static_cast<int>(getTime(date));
    QWidget* widget = activityLayout->itemAt(time)->widget();
    CategoryWidget* categoryWidget = static_cast<CategoryWidget*>(widget);
    categoryWidget->updateStatus();

    categoryWidget->setVisible(categoryWidget->hasChatrooms());
}

// update widget after add/delete/hide/show
void FriendListWidget::reDraw()
{
    hide();
    show();
    resize(QSize()); // lifehack
}

CircleWidget* FriendListWidget::createCircleWidget(int id)
{
    if (id == -1)
        id = Settings::getInstance().addCircle();

    // Stop, after it has been created. Code after this is for displaying.
    if (mode == Activity)
        return nullptr;

    assert(circleLayout != nullptr);

    CircleWidget* circleWidget = new CircleWidget(this, id);
    circleLayout->addSortedWidget(circleWidget);
    connect(this, &FriendListWidget::onCompactChanged, circleWidget, &CircleWidget::onCompactChanged);
    connect(circleWidget, &CircleWidget::renameRequested, this, &FriendListWidget::renameCircleWidget);
    circleWidget->show(); // Avoid flickering.

    return circleWidget;
}

QLayout* FriendListWidget::nextLayout(QLayout* layout, bool forward) const
{
    if (layout == groupLayout.getLayout()) {
        if (forward) {
            if (groupsOnTop)
                return listLayout->getLayoutOnline();

            return listLayout->getLayoutOffline();
        } else {
            if (groupsOnTop)
                return circleLayout->getLayout();

            return listLayout->getLayoutOnline();
        }
    } else if (layout == listLayout->getLayoutOnline()) {
        if (forward) {
            if (groupsOnTop)
                return listLayout->getLayoutOffline();

            return groupLayout.getLayout();
        } else {
            if (groupsOnTop)
                return groupLayout.getLayout();

            return circleLayout->getLayout();
        }
    } else if (layout == listLayout->getLayoutOffline()) {
        if (forward)
            return circleLayout->getLayout();
        else if (groupsOnTop)
            return listLayout->getLayoutOnline();

        return groupLayout.getLayout();
    } else if (layout == circleLayout->getLayout()) {
        if (forward) {
            if (groupsOnTop)
                return groupLayout.getLayout();

            return listLayout->getLayoutOnline();
        } else
            return listLayout->getLayoutOffline();
    }
    return nullptr;
}
