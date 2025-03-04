﻿// Copyright (c) 2022 NetEase, Inc. All rights reserved.
// Use of this source code is governed by a MIT license that can be
// found in the LICENSE file.

#include "members_manager.h"
#include "../../models/members_model.h"
#include "manager/global_manager.h"
#include "manager/meeting/share_manager.h"
#include "manager/meeting/whiteboard_manager.h"

MembersManager::MembersManager(QObject* parent)
    : QObject(parent) {
    qRegisterMetaType<NEMeeting::HandsUpStatus>();
    m_membersController = std::make_shared<NEMeetingUserController>();

    auto globalConfig = GlobalManager::getInstance()->getGlobalConfig();
    auto galleryViewPageSize = globalConfig->getGalleryPageSize();
    setGalleryViewPageSize(galleryViewPageSize);

    connect(this, &MembersManager::afterUserJoined, this, &MembersManager::onAfterUserJoinedUI);
    connect(this, &MembersManager::afterUserLeft, this, &MembersManager::onAfterUserLeftUI);

    connect(&m_refreshTime, &QTimer::timeout, this, [this]() { getMembersPaging(m_pageSize, m_currentPage); });
    m_refreshTime.setSingleShot(true);
}

MembersManager::~MembersManager() {
    this->disconnect();
}

QString MembersManager::getNicknameByAccountId(const QString& accountId) {
    for (int i = 0; i < m_items.size(); i++) {
        auto item = m_items.at(i);
        if (item.accountId == accountId) {
            return item.nickname;
        }
    }
    return "";
}

bool MembersManager::getMyHandsupStatus() {
    for (int i = 0; i < m_items.size(); i++) {
        auto item = m_items.at(i);
        if (item.accountId == AuthManager::getInstance()->authAccountId()) {
            bool status = item.handsupStatus == NEMeeting::HAND_STATUS_RAISE;
            setHandsUpStatus(status);
            return status;
        }
    }
    return false;
}

bool MembersManager::isManagerRoleEx(const QString& accountId) {
    return m_managerList.contains(accountId);
}

void MembersManager::initMemberList(const SharedMemberPtr& localMember, const std::list<SharedMemberPtr>& remoteMemberList) {
    onUserJoined(localMember, false);
    for (auto member : remoteMemberList) {
        onUserJoined(member, false);
    }
}

void MembersManager::resetManagerList() {
    m_managerList.clear();
    setIsManagerRole(false);
    // emit managerAccountIdChanged(AuthManager::getInstance()->authAccountId(), false);
}

void MembersManager::onUserJoined(const SharedMemberPtr& member, bool bNotify) {
    m_invoker.execute([=]() {
        for (auto item : m_items) {
            if (member->getUserUuid() == item.accountId.toStdString()) {
                emit userReJoined(item.accountId);
                return;
            }
        }

        // todo 影子用户角色名
        NERoleType roleType = getRoleType(member->getUserRole().name);
        if (kRoleHiding != roleType /*&& member->getIsInRtcChannel()*/) {
            Q_EMIT preItemAppended();
            MemberInfo info;
            info.accountId = QString::fromStdString(member->getUserUuid());
            info.roleType = roleType;
            if (info.roleType == kRoleHost) {
                setHostAccountId(info.accountId);
                MeetingManager::getInstance()->getMeetingController()->updateHostAccountId(info.accountId.toStdString());
            } else if (info.roleType == kRoleManager) {
                onManagerChanged(info.accountId.toStdString(), true);
            }

            info.clientType = member->getClientType();
            info.audioStatus = member->getIsAudioOn() ? 1 : 2;
            info.videoStatus = member->getIsVideoOn() ? 1 : 2;
            info.nickname = QString::fromStdString(member->getUserName());
            info.sharing = member->getIsSharingScreen();
            info.isWhiteboardEnable = false;
            info.isWhiteboardShareOwner = info.accountId == WhiteboardManager::getInstance()->whiteboardSharerAccountId();
            convertPropertiesToMember(member->getProperties(), info);

            if (info.accountId == AuthManager::getInstance()->authAccountId()) {
                AudioManager::getInstance()->setLocalAudioStatus(member->getIsAudioOn() ? NEMeeting::DEVICE_ENABLED
                                                                                        : NEMeeting::DEVICE_DISABLED_BY_DELF);
                VideoManager::getInstance()->setLocalVideoStatus(member->getIsVideoOn() ? NEMeeting::DEVICE_ENABLED
                                                                                        : NEMeeting::DEVICE_DISABLED_BY_DELF);
                m_items.insert(0, info);
            } else {
                m_items.append(info);
            }
            Q_EMIT postItemAppended();

            setCount(m_items.size());
            emit afterUserJoined(QString::fromStdString(member->getUserUuid()), QString::fromStdString(member->getUserName()), bNotify);
        }
    });
}

void MembersManager::onUserLeft(const SharedMemberPtr& member) {
    m_invoker.execute([=]() {
        QString qstrAccountId = QString::fromStdString(member->getUserUuid());
        for (int i = 0; i < m_items.size(); i++) {
            auto item = m_items.at(i);
            if (item.accountId == qstrAccountId) {
                if (item.accountId == m_hostAccountId) {
                    setHostAccountId("");
                    MeetingManager::getInstance()->getMeetingController()->updateHostAccountId("");
                } else if (item.roleType == kRoleManager) {
                    onManagerChanged(item.accountId.toStdString(), false);
                }

                if (member->getUserUuid() == MeetingManager::getInstance()->getMeetingController()->getRoomInfo().focusAccountId) {
                    MeetingManager::getInstance()->getMeetingController()->updateFocusAccountId("");
                    VideoManager::getInstance()->onFocusVideoChanged("", false);
                }

                if (item.accountId == ShareManager::getInstance()->shareAccountId()) {
                    ShareManager::getInstance()->onRoomUserScreenShareStatusChanged(item.accountId.toStdString(), kNERoomScreenShareStatusEnd);
                }
                if (item.handsupStatus == NEMeeting::HAND_STATUS_RAISE) {
                    m_nHandsUpCount--;
                    emit handsUpCountChange();
                    emit handsupStatusChanged(qstrAccountId, NEMeeting::HAND_STATUS_DOWN);
                }
                Q_EMIT preItemRemoved(i);
                m_items.remove(i);
                Q_EMIT postItemRemoved();

                setCount(m_items.size());
                emit afterUserLeft(QString::fromStdString(member->getUserUuid()), QString::fromStdString(member->getUserName()));
                break;
            }
        }
    });
}

void MembersManager::onHostChanged(const std::string& hostAccountId) {
    m_invoker.execute([=]() {
        if (AuthManager::getInstance()->getAuthInfo().accountId == hostAccountId) {
            if (m_bHandsUp) {
                handsUp(false);
            }
        }

        QMetaObject::invokeMethod(this, "onHostChangedUI", Qt::AutoConnection, Q_ARG(QString, QString::fromStdString(hostAccountId)));
    });
}

void MembersManager::onManagerChanged(const std::string& managerAccountId, bool bAdd) {
    m_invoker.execute([=]() {
        QString qstrManagerAccountId = QString::fromStdString(managerAccountId);
        if (bAdd) {
            m_managerList << qstrManagerAccountId;
            if (AuthManager::getInstance()->authAccountId() == qstrManagerAccountId) {
                setIsManagerRole(true);
                if (m_bHandsUp) {
                    handsUp(false);
                }
            }
        } else {
            for (auto iter = m_managerList.begin(); iter != m_managerList.end(); ++iter) {
                if (iter == qstrManagerAccountId) {
                    m_managerList.erase(iter);
                    break;
                }
            }

            if (AuthManager::getInstance()->authAccountId() == qstrManagerAccountId) {
                setIsManagerRole(false);
            }
        }

        emit managerAccountIdChanged(qstrManagerAccountId, bAdd);
    });
}

void MembersManager::onMemberNicknameChanged(const std::string& accountId, const std::string& nickname) {
    QMetaObject::invokeMethod(this, "handleNicknameChanged", Qt::AutoConnection, Q_ARG(QString, QString::fromStdString(accountId)),
                              Q_ARG(QString, QString::fromStdString(nickname)));
}

std::shared_ptr<NEMeetingUserController> MembersManager::getUserController() {
    return m_membersController;
}

bool MembersManager::getPrimaryMember(MemberInfo& memberInfo) {
    auto membersCount = m_items.size();
    auto meetingInfo = MeetingManager::getInstance()->getMeetingInfo();
    auto authInfo = AuthManager::getInstance()->getAuthInfo();
    std::string primary;
    do {
        if (!meetingInfo.screenSharingUserId.empty()) {
            primary = meetingInfo.screenSharingUserId;
            YXLOG(Info) << "Select screen sharing member as primary member: " << primary << YXLOGEnd;
            break;
        }
        if (!meetingInfo.focusAccountId.empty()) {
            primary = meetingInfo.focusAccountId;
            YXLOG(Info) << "Select focus member as primary member: " << primary << YXLOGEnd;
            break;
        }
        if (!meetingInfo.speakerUserId.empty() && membersCount > 2 && meetingInfo.speakerUserId != authInfo.accountId) {
            primary = meetingInfo.speakerUserId;
            YXLOG(Info) << "Select active speaker member as primary member: " << primary << YXLOGEnd;
            break;
        }
        if (!meetingInfo.hostAccountId.empty() && membersCount > 2 && meetingInfo.hostAccountId != authInfo.accountId) {
            primary = meetingInfo.hostAccountId;
            YXLOG(Info) << "Select meeting host member as primary member: " << primary << YXLOGEnd;
            break;
        }

        if (m_items.size() > 0) {
            primary = m_items.last().accountId.toStdString();
        }

        YXLOG(Info) << "Select the last member to join as primary member: " << primary << YXLOGEnd;
    } while (false);

    return getMemberByAccountId(QString::fromStdString(primary), memberInfo);
}

void MembersManager::updateMembersPaging() {
    getMembersPaging(m_pageSize, m_currentPage);
}

bool MembersManager::handsUpStatus() const {
    return m_bHandsUp;
}

void MembersManager::setHandsUpStatus(bool handsUp) {
    m_bHandsUp = handsUp;
}

void MembersManager::getMembersPaging(quint32 pageSize, quint32 pageNumber) {
    if (isWhiteboardView()) {
        pagingWhiteboardView(pageSize, pageNumber);
    } else if (isGalleryView()) {
        pagingGalleryView(pageSize, pageNumber);
    } else {
        pagingFocusView(pageSize, pageNumber);
    }
}

void MembersManager::setAsHost(const QString& accountId) {
    auto roomContext = MeetingManager::getInstance()->getRoomContext();
    if (roomContext) {
        roomContext->handOverMyRole(accountId.toStdString(),
                                    std::bind(&MeetingManager::onError, MeetingManager::getInstance(), std::placeholders::_1, std::placeholders::_2));
    }
}

void MembersManager::setAsManager(const QString& accountId, const QString& nickname) {
    auto roomContext = MeetingManager::getInstance()->getRoomContext();
    if (roomContext) {
        roomContext->changeMemberRole(accountId.toStdString(), "cohost", [=](int code, const std::string& msg) {
            if (code == 0) {
                emit managerUpdateSuccess(nickname, true);
            } else {
                QString errorMsg = QString::fromStdString(msg);
                if (code == 1002) {
                    errorMsg = tr("The assigned role exceeds the number limit");
                }
                MeetingManager::getInstance()->onError(code, errorMsg.toStdString());
            }
        });
    }
}

void MembersManager::setAsMember(const QString& accountId, const QString& nickname) {
    auto roomContext = MeetingManager::getInstance()->getRoomContext();
    if (roomContext) {
        roomContext->changeMemberRole(accountId.toStdString(), "member", [=](int code, const std::string& msg) {
            MeetingManager::getInstance()->onError(code, msg);
            if (code == 0) {
                emit managerUpdateSuccess(nickname, false);
            }
        });
    }
}

void MembersManager::setAsFocus(const QString& accountId, bool set) {
    QByteArray byteAccountId = accountId.toUtf8();
    auto roomContext = MeetingManager::getInstance()->getRoomContext();
    if (roomContext) {
        QString value = set ? accountId : "";
        roomContext->updateRoomProperty(
            "focus", value.toStdString(), accountId.toStdString(),
            std::bind(&MeetingManager::onError, MeetingManager::getInstance(), std::placeholders::_1, std::placeholders::_2));
    }
}

void MembersManager::kickMember(const QString& accountId) {
    QByteArray byteAccountId = accountId.toUtf8();
    m_membersController->removeUser(byteAccountId.data(),
                                    std::bind(&MeetingManager::onError, MeetingManager::getInstance(), std::placeholders::_1, std::placeholders::_2));
}

void MembersManager::pagingFocusView(quint32 pageSize, quint32 pageNumber) {
    QVector<MemberInfo> members = getMembersByRoleType();
    if (members.count() == 0)
        return;

    MemberInfo primaryMemberInfo;
    if (!getPrimaryMember(primaryMemberInfo)) {
        return;
    }

    QVector<MemberInfo> secondaryMembers;
    auto meetingInfo = MeetingManager::getInstance()->getMeetingInfo();
    std::string strSharingAccountId = meetingInfo.screenSharingUserId;
    if (strSharingAccountId.empty()) {
        for (auto& member : members) {
            if (member.accountId == primaryMemberInfo.accountId)
                continue;
            secondaryMembers.push_back(member);
        }
    } else {
        auto authInfo = AuthManager::getInstance()->getAuthInfo();
        if (strSharingAccountId != authInfo.accountId) {
            auto it = std::find_if(members.begin(), members.end(),
                                   [strSharingAccountId](const MemberInfo& info) { return info.accountId.toStdString() == strSharingAccountId; });
            if (members.end() != it) {
                secondaryMembers.push_back(*it);
                for (auto& member : members) {
                    if (it->accountId == member.accountId)
                        continue;
                    secondaryMembers.push_back(member);
                }
            } else {
                secondaryMembers = members;
            }
        } else {
            secondaryMembers = members;
        }
    }

    uint32_t pageCount = (uint32_t)secondaryMembers.size() <= pageSize
                             ? 1
                             : (uint32_t)secondaryMembers.size() / pageSize + ((uint32_t)secondaryMembers.size() % pageSize == 0 ? 0 : 1);
    m_currentPage = pageNumber > pageCount ? pageCount : pageNumber;
    m_pageSize = pageSize;

    QJsonArray pagedMembers;
    int begin = (m_currentPage - 1) * pageSize;
    int end = begin + (int)pageSize > (int)secondaryMembers.size() ? (int)secondaryMembers.size() : begin + (int)pageSize;
    YXLOG(Info) << "Get members from cache list: " << begin << " to " << end << ", current page: " << m_currentPage << ", page count: " << pageCount
                << ", page size: " << m_pageSize << YXLOGEnd;
    for (int i = begin; i < end; i++) {
        auto member_item = secondaryMembers[i];
        QJsonObject member;
        member[kMemberAccountId] = member_item.accountId;
        member[kMemberNickname] = member_item.nickname;
        member[kMemberAudioStatus] = member_item.audioStatus;
        member[kMemberVideoStatus] = member_item.videoStatus;
        member[kMemberSharingStatus] = member_item.sharing;
        member[kMemberAudioHandsUpStatus] = member_item.handsupStatus;
        member[kMemberClientType] = member_item.clientType;
        pagedMembers.push_back(member);
    }

    QJsonObject primaryMember;
    primaryMember[kMemberAccountId] = primaryMemberInfo.accountId;
    primaryMember[kMemberNickname] = primaryMemberInfo.nickname;
    primaryMember[kMemberAudioStatus] = primaryMemberInfo.audioStatus;
    primaryMember[kMemberVideoStatus] = primaryMemberInfo.videoStatus;
    primaryMember[kMemberSharingStatus] = primaryMemberInfo.sharing;
    primaryMember[kMemberAudioHandsUpStatus] = primaryMemberInfo.handsupStatus;
    primaryMember[kMemberClientType] = primaryMemberInfo.clientType;

    YXLOG(Debug) << "-------------------------------------------------" << YXLOGEnd;
    YXLOG(Debug) << "Focus view, primary member: " << primaryMemberInfo.accountId.toStdString() << YXLOGEnd;
    for (int i = 0; i < pagedMembers.size(); i++) {
        const QJsonObject& member = pagedMembers.at(i).toObject();
        YXLOG(Debug) << "Secondary members: " << i << ", member info: " << member[kMemberAccountId].toString().toStdString() << YXLOGEnd;
    }
    YXLOG(Debug) << "-------------------------------------------------" << YXLOGEnd;

    emit membersChanged(primaryMember, pagedMembers, m_currentPage, secondaryMembers.size());
}

void MembersManager::pagingWhiteboardView(quint32 pageSize, quint32 pageNumber) {
    QVector<MemberInfo> members = getMembersByRoleType();
    if (members.count() == 0)
        return;

    QVector<MemberInfo> secondaryMembers;
    MemberInfo whiteboardMember;
    for (auto& member : members) {
        if (member.accountId == WhiteboardManager::getInstance()->whiteboardSharerAccountId()) {
            whiteboardMember = member;
        } else {
            secondaryMembers.push_back(member);
        }
    }

    if (!whiteboardMember.accountId.isEmpty()) {
        secondaryMembers.insert(secondaryMembers.begin(), whiteboardMember);
    }

    uint32_t pageCount =
        secondaryMembers.size() <= pageSize ? 1 : secondaryMembers.size() / pageSize + (secondaryMembers.size() % pageSize == 0 ? 0 : 1);
    m_currentPage = pageNumber > pageCount ? pageCount : pageNumber;
    m_pageSize = pageSize;

    QJsonArray pagedMembers;
    int begin = (m_currentPage - 1) * pageSize;
    int end = begin + pageSize > (int)secondaryMembers.size() ? (int)secondaryMembers.size() : begin + pageSize;

    for (int i = begin; i < end; i++) {
        auto member_item = secondaryMembers[i];
        QJsonObject member;
        member[kMemberAccountId] = member_item.accountId;
        member[kMemberNickname] = member_item.nickname;
        member[kMemberAudioStatus] = member_item.audioStatus;
        member[kMemberVideoStatus] = member_item.videoStatus;
        member[kMemberSharingStatus] = member_item.sharing;
        pagedMembers.push_back(member);
    }

    emit membersChanged(QJsonObject(), pagedMembers, m_currentPage, members.size());
}

void MembersManager::pagingGalleryView(quint32 pageSize, quint32 pageNumber) {
    QVector<MemberInfo> members = getMembersByRoleType();
    if (members.count() == 0)
        return;

    QVector<MemberInfo> secondaryMembers;
    auto authInfo = AuthManager::getInstance()->getAuthInfo();
    for (auto& member : members) {
        secondaryMembers.push_back(member);
    }

    uint32_t pageCount =
        secondaryMembers.size() <= pageSize ? 1 : secondaryMembers.size() / pageSize + (secondaryMembers.size() % pageSize == 0 ? 0 : 1);
    m_currentPage = pageNumber > pageCount ? pageCount : pageNumber;
    m_pageSize = pageSize;

    QJsonArray pagedMembers;
    int begin = (m_currentPage - 1) * pageSize;
    int end = begin + pageSize > secondaryMembers.size() ? secondaryMembers.size() : begin + pageSize;
    YXLOG(Info) << "Get members from cache list: " << begin << " to " << end << ", current page: " << m_currentPage << ", page count: " << pageCount
                << ", page size: " << m_pageSize << YXLOGEnd;
    for (int i = begin; i < end; i++) {
        auto member_item = secondaryMembers[i];
        QJsonObject member;
        member[kMemberAccountId] = member_item.accountId;
        member[kMemberNickname] = member_item.nickname;
        member[kMemberAudioStatus] = member_item.audioStatus;
        member[kMemberVideoStatus] = member_item.videoStatus;
        member[kMemberSharingStatus] = member_item.sharing;
        pagedMembers.push_back(member);
    }

    //        QJsonObject selfMember;
    //        auto& firstMember = members.front();
    //        selfMember[kMemberAccountId] = firstMember.accountId;
    //        selfMember[kMemberNickname] = firstMember.nickname;
    //        selfMember[kMemberAudioStatus] = firstMember.audioStatus;
    //        selfMember[kMemberVideoStatus] = firstMember.videoStatus;
    //        selfMember[kMemberSharingStatus] = firstMember.sharing;
    //        pagedMembers.push_front(selfMember);

    YXLOG(Debug) << "-------------------------------------------------" << YXLOGEnd;
    for (int i = 0; i < pagedMembers.size(); i++) {
        const QJsonObject& member = pagedMembers.at(i).toObject();
        YXLOG(Debug) << "Gallery view members: " << i << ", member info: " << member[kMemberAccountId].toString().toStdString() << YXLOGEnd;
    }
    YXLOG(Debug) << "-------------------------------------------------" << YXLOGEnd;

    emit membersChanged(QJsonObject(), pagedMembers, m_currentPage, members.size());
}

void MembersManager::convertPropertiesToMember(const std::map<std::string, std::string>& properties, MemberInfo& info) {
    qInfo() << "convertPropertiesToMember properties:" << properties.size();
    auto iter = properties.begin();
    for (; iter != properties.end(); iter++) {
        if (iter->first == "tag") {
            info.tag = QString::fromStdString(iter->second);
        } else if (iter->first == "handsUp") {
            auto tagJson = iter->second;
            QString status = QString::fromStdString(iter->second);
            if (status == "0") {
                info.handsupStatus = NEMeeting::HAND_STATUS_DOWN;
            } else if (status == "1") {
                info.handsupStatus = NEMeeting::HAND_STATUS_RAISE;
                m_nHandsUpCount++;
            } else if (status == "2") {
                info.handsupStatus = NEMeeting::HAND_STATUS_REJECT;
            }
        }
    }
}

bool MembersManager::getMemberByAccountId(const QString& accountId, MemberInfo& memberInfo) {
    for (auto item : m_items) {
        if (accountId == item.accountId) {
            memberInfo = item;
            return true;
        }
    }

    return false;
}

QVector<MemberInfo> MembersManager::getMembersByRoleType(const QVector<NERoleType>& roleType) {
    QVector<MemberInfo> members;
    for (auto& member : m_items) {
        auto it = std::find_if(roleType.begin(), roleType.end(), [member](const auto& it) { return member.roleType == it; });
        if (roleType.end() != it) {
            members.push_back(member);
        }
    }
    return members;
}

void MembersManager::onAfterUserJoinedUI(const QString& accountId, const QString& nickname, bool bNotify) {
    if (m_refreshTime.isActive())
        m_refreshTime.stop();
    m_refreshTime.start(500);
    if (accountId != AuthManager::getInstance()->authAccountId() && bNotify) {
        emit userJoinNotify(nickname);
    }
    emit countChanged();
}

void MembersManager::onAfterUserLeftUI(const QString& accountId, const QString& nickname) {
    if (m_refreshTime.isActive())
        m_refreshTime.stop();
    m_refreshTime.start(500);
    emit userLeftNotify(nickname);
    emit countChanged();
}

void MembersManager::onHostChangedUI(const QString& hostAccountId) {
    emit hostAccountIdChangedSignal(hostAccountId, m_hostAccountId);
    setHostAccountId(hostAccountId);
}

void MembersManager::handleAudioStatusChanged(const QString& accountId, int status) {
    for (int i = 0; i < m_items.size(); i++) {
        auto item = m_items.at(i);
        if (item.accountId == accountId) {
            int statusTmp = item.audioStatus;
            item.audioStatus = status;
            m_items[i] = item;
            if ((1 == statusTmp && 1 != status) || (1 != statusTmp && 1 == status)) {
                Q_EMIT dataChanged(i, MembersModel::kMemberRoleAudio);
            }
            break;
        }
    }
}

void MembersManager::handleVideoStatusChanged(const QString& accountId, int status) {
    for (int i = 0; i < m_items.size(); i++) {
        auto item = m_items.at(i);
        if (item.accountId == accountId) {
            int statusTmp = item.videoStatus;
            item.videoStatus = status;
            m_items[i] = item;
            if ((1 == statusTmp && 1 != status) || (1 != statusTmp && 1 == status)) {
                Q_EMIT dataChanged(i, MembersModel::kMemberRoleVideo);
            }
            break;
        }
    }
}

void MembersManager::handleHandsupStatusChanged(const QString& accountId, NEMeeting::HandsUpStatus status) {
    for (int i = 0; i < m_items.size(); i++) {
        auto item = m_items.at(i);
        if (item.accountId == accountId) {
            int statusTmp = item.handsupStatus;
            item.handsupStatus = status;
            m_items[i] = item;
            if ((NEMeeting::HAND_STATUS_RAISE == statusTmp && NEMeeting::HAND_STATUS_RAISE != status) ||
                (NEMeeting::HAND_STATUS_RAISE != statusTmp && NEMeeting::HAND_STATUS_RAISE == status)) {
                Q_EMIT dataChanged(i, MembersModel::kMemberRoleHansUpStatus);
            }
            break;
        }
    }
}

void MembersManager::handleMeetingStatusChanged(NEMeeting::Status status, int /*errorCode*/, const QString& /*errorMessage*/) {
    if (status == NEMeeting::MEETING_IDLE || status == NEMeeting::MEETING_ENDED || status == NEMeeting::MEETING_RECONNECT_FAILED ||
        status == NEMeeting::MEETING_CONNECT_FAILED || status == NEMeeting::MEETING_DISCONNECTED || status == NEMeeting::MEETING_KICKOUT_BY_HOST ||
        status == NEMeeting::MEETING_MULTI_SPOT_LOGIN)
        m_items.clear();
}

void MembersManager::handleWhiteboardDrawEnableChanged(const QString& sharedAccountId, bool enable) {
    for (int i = 0; i < m_items.size(); i++) {
        auto item = m_items.at(i);
        if (item.accountId == sharedAccountId) {
            bool statusTmp = item.isWhiteboardEnable;
            item.isWhiteboardEnable = enable;
            m_items[i] = item;
            if (statusTmp != enable) {
                Q_EMIT dataChanged(i, MembersModel::kMemberRoleWhiteboard);
            }
            break;
        }
    }
}

void MembersManager::handleNicknameChanged(const QString& accountId, const QString& nickname) {
    emit nicknameChanged(accountId, nickname);
    for (int i = 0; i < m_items.size(); i++) {
        auto item = m_items.at(i);
        if (item.accountId == accountId) {
            QString statusTmp = item.nickname;
            item.nickname = nickname;
            m_items[i] = item;
            if (statusTmp != nickname) {
                Q_EMIT dataChanged(i, MembersModel::kMemberRoleNickname);
            }
            break;
        }
    }
}

void MembersManager::handleShareAccountIdChanged() {
    auto meetingInfo = MeetingManager::getInstance()->getMeetingInfo();
    QString accountId = QString::fromStdString(meetingInfo.screenSharingUserId);
    bool sharing = !accountId.isEmpty();

    if (sharing) {
        for (int i = 0; i < m_items.size(); i++) {
            auto item = m_items.at(i);
            if (item.accountId == accountId) {
                item.sharing = true;
                m_items[i] = item;
                Q_EMIT dataChanged(i, MembersModel::kMemberRoleSharing);
                break;
            }
        }
    } else {
        for (int i = 0; i < m_items.size(); i++) {
            auto item = m_items.at(i);
            if (item.sharing) {
                item.sharing = false;
                m_items[i] = item;
                Q_EMIT dataChanged(i, MembersModel::kMemberRoleSharing);
                break;
            }
        }
    }
}

void MembersManager::handleAudioVolumeIndication(const QString& accountId, int volume) {
    for (int i = 0; i < m_items.size(); i++) {
        auto item = m_items.at(i);
        if (item.accountId == accountId) {
            if (item.audioStatus != 1) {
                break;
            }

            int temp = item.audioVolume;
            item.audioVolume = volume;
            m_items[i] = item;
            if (temp != volume) {
                Q_EMIT dataChanged(i, MembersModel::kMemberRoleAudioVolume);
            }
            break;
        }
    }
}

void MembersManager::handleRoleChanged(const QString& accountId, NERoleType roleType) {
    for (int i = 0; i < m_items.size(); i++) {
        auto item = m_items.at(i);
        if (item.accountId == accountId) {
            if (item.roleType != roleType) {
                item.roleType = roleType;
                m_items[i] = item;
                Q_EMIT dataChanged(i, MembersModel::kMemberRoleType);
                break;
            }
        }
    }
}

void MembersManager::allowRemoteMemberHandsUp(const QString& accountId, bool bAllowHandsUp) {
    QByteArray byteAccountId = accountId.toUtf8();
    m_membersController->lowerHand(byteAccountId.data(),
                                   std::bind(&MeetingManager::onError, MeetingManager::getInstance(), std::placeholders::_1, std::placeholders::_2));
}

void MembersManager::handsUp(bool bHandsUp) {
    m_membersController->raiseMyHand(
        bHandsUp, std::bind(&MeetingManager::onError, MeetingManager::getInstance(), std::placeholders::_1, std::placeholders::_2));
}

void MembersManager::muteRemoteVideoAndAudio(const QString& accountId, bool mute) {
    QByteArray byteAccountId = accountId.toUtf8();
    m_membersController->muteParticipantAudioAndVideo(
        byteAccountId.data(), mute, std::bind(&MeetingManager::onError, MeetingManager::getInstance(), std::placeholders::_1, std::placeholders::_2));
}

void MembersManager::onHandsUpStatusChangedUI(const QString& accountId, NEMeeting::HandsUpStatus status) {
    YXLOG(Info) << "onHandsUpStatusChangedUI accountId = " << accountId.toStdString() << " audio handstatus : " << status << YXLOGEnd;
    auto authInfo = AuthManager::getInstance()->getAuthInfo();
    if (authInfo.accountId == accountId.toStdString()) {
        if (status == NEMeeting::HAND_STATUS_RAISE) {
            m_bHandsUp = true;
        } else {
            m_bHandsUp = false;
        }
    }

    if (status == NEMeeting::HAND_STATUS_RAISE) {
        m_nHandsUpCount++;
    } else {
        m_nHandsUpCount--;
    }

    if (authInfo.accountId == m_hostAccountId.toStdString()) {
        emit handsUpCountChange();
    }

    emit handsupStatusChanged(accountId, status);
    handleHandsupStatusChanged(accountId, status);
}

void MembersManager::onRoleChangedUI(const QString& accountId, const QString& strRoleType) {
    handleRoleChanged(accountId, getRoleType(strRoleType.toStdString()));
}

uint32_t MembersManager::count() const {
    return m_items.count();
}

void MembersManager::setCount(const uint32_t& count) {
    m_count = count;
}

int MembersManager::handsUpCount() const {
    return m_nHandsUpCount;
}

void MembersManager::setHandsUpCount(int count) {
    m_nHandsUpCount = count;
    emit handsUpCountChange();
}

void MembersManager::resetWhiteboardDrawEnable() {
    if (m_membersController) {
        for (int i = 0; i < m_items.size(); i++) {
            auto item = m_items.at(i);
            if (item.isWhiteboardEnable) {
                item.isWhiteboardEnable = false;
                m_items[i] = item;
                Q_EMIT dataChanged(i, MembersModel::kMemberRoleWhiteboard);
            }
        }
    }
}

void MembersManager::updateWhiteboardOwner(const QString& whiteboardOwner, bool isSharing) {
    for (int i = 0; i < m_items.size(); i++) {
        auto item = m_items.at(i);
        if (item.accountId == whiteboardOwner) {
            item.isWhiteboardShareOwner = isSharing;
            m_items[i] = item;
            Q_EMIT dataChanged(i, MembersModel::kMemberWhiteboardShareOwner);
        }
    }
}

int MembersManager::galleryViewPageSize() const {
    return m_galleryViewPageSize;
}

void MembersManager::setGalleryViewPageSize(const int& galleryViewPageSize) {
    m_galleryViewPageSize = galleryViewPageSize;
    emit galleryViewPageSizeChanged();
}

NEMeeting::HandsUpType MembersManager::handsUpType() const {
    return m_handsUpType;
}

void MembersManager::setHandsUpType(NEMeeting::HandsUpType type) {
    m_handsUpType = type;
}

void MembersManager::onNetworkQuality(const std::string& accountId, NERoomRtcNetWorkQuality up, NERoomRtcNetWorkQuality down) {
    // 只通知当前登录的用户网络状况
    if (AuthManager::getInstance()->authAccountId().toStdString() != accountId || kNERoomRtcNetworkQualityUnknown == up ||
        kNERoomRtcNetworkQualityUnknown == down) {
        return;
    }

    auto nwt = netWorkQualityType(up, down);
    auto localMember = std::find_if(m_items.begin(), m_items.end(),
                                    [](const MemberInfo& it) { return it.accountId == AuthManager::getInstance()->authAccountId(); });
    if (localMember != m_items.end()) {
        localMember->netWorkQualityType = nwt;
    }

    emit netWorkQualityTypeChanged(nwt);
}

void MembersManager::onHandsUpStatusChanged(const std::string& accountId, NEMeeting::HandsUpStatus handsUpStatus) {
    QMetaObject::invokeMethod(this, "onHandsUpStatusChangedUI", Qt::AutoConnection, Q_ARG(QString, QString::fromStdString(accountId)),
                              Q_ARG(NEMeeting::HandsUpStatus, handsUpStatus));
}

void MembersManager::onRoleChanged(const std::string& accountId, const std::string& beforeRole, const std::string& afterRole) {
    NERoleType newRoleType = getRoleType(afterRole);
    NERoleType oldRoleType = getRoleType(beforeRole);
    QString qstrAccountId = QString::fromStdString(accountId);

    if (newRoleType == kRoleHost) {
        MeetingManager::getInstance()->getMeetingController()->updateHostAccountId(accountId);
        onHostChanged(accountId);
    } else if (newRoleType == kRoleManager) {
        onManagerChanged(accountId, true);
    }

    if (oldRoleType == kRoleManager) {
        onManagerChanged(accountId, false);
    }

    QMetaObject::invokeMethod(this, "onRoleChangedUI", Qt::AutoConnection, Q_ARG(QString, QString::fromStdString(accountId)),
                              Q_ARG(QString, QString::fromStdString(afterRole)));
}

QVector<MemberInfo> MembersManager::items() const {
    return m_items;
}

int MembersManager::netWorkQualityType() const {
    auto localMember = std::find_if(m_items.begin(), m_items.end(),
                                    [](const MemberInfo& it) { return it.accountId == AuthManager::getInstance()->authAccountId(); });
    if (localMember != m_items.end()) {
        return localMember->netWorkQualityType;
    }

    return NEMeeting::NETWORKQUALITY_GOOD;
}

NEMeeting::NetWorkQualityType MembersManager::netWorkQualityType(NERoomRtcNetWorkQuality upNetWorkQuality,
                                                                 NERoomRtcNetWorkQuality downNetWorkQuality) const {
    if ((kNERoomRtcNetworkQualityVeryBad == upNetWorkQuality || kNERoomRtcNetworkQualityDown == upNetWorkQuality) ||
        (kNERoomRtcNetworkQualityVeryBad == downNetWorkQuality || kNERoomRtcNetworkQualityDown == downNetWorkQuality)) {
        return NEMeeting::NETWORKQUALITY_BAD;
    } else if ((kNERoomRtcNetworkQualityExcellent == upNetWorkQuality || kNERoomRtcNetworkQualityGood == upNetWorkQuality) &&
               (kNERoomRtcNetworkQualityExcellent == downNetWorkQuality || kNERoomRtcNetworkQualityGood == downNetWorkQuality)) {
        return NEMeeting::NETWORKQUALITY_GOOD;
    } else {
        return NEMeeting::NETWORKQUALITY_GENERAL;
    }
}

QString MembersManager::hostAccountId() const {
    return m_hostAccountId;
}

void MembersManager::setHostAccountId(const QString& hostAccountId) {
    m_hostAccountId = hostAccountId;
    emit hostAccountIdChanged();
    auto anthInfo = AuthManager::getInstance()->getAuthInfo();
    AuthManager::getInstance()->setIsHostAccount(anthInfo.accountId == hostAccountId.toStdString());

    //刷新成员列表排序
    if (m_membersController) {
        for (int i = 0; i < m_items.size(); i++) {
            auto item = m_items.at(i);
            if (item.accountId == m_hostAccountId) {
                Q_EMIT dataChanged(i, -1);
                break;
            }
        }
    }
}

bool MembersManager::isGalleryView() const {
    return m_isGalleryView;
}

void MembersManager::setIsGalleryView(bool isGellaryView) {
    m_isGalleryView = isGellaryView;
    if (isGellaryView) {
        auto globalConfig = GlobalManager::getInstance()->getGlobalConfig();
        auto galleryViewPageSize = globalConfig->getGalleryPageSize();
        setGalleryViewPageSize(galleryViewPageSize);
    }

    emit isGalleryViewChanged();
}

bool MembersManager::isWhiteboardView() const {
    return m_isWhiteboardView;
}

void MembersManager::setIsWhiteboardView(bool isWhiteboardView) {
    m_isWhiteboardView = isWhiteboardView;
    emit isWhiteboardViewChanged();
}

NERoleType MembersManager::getRoleType(const std::string& strRoleType) {
    if ("host" == strRoleType) {
        return kRoleHost;
    } else if ("cohost" == strRoleType) {
        return kRoleManager;
    } else if ("member" == strRoleType) {
        return kRoleMember;
    } else {
        return kRoleInvaild;
    }
}

bool MembersManager::isManagerRole() {
    return m_isManagerRole;
}

void MembersManager::setIsManagerRole(bool isManagerRole) {
    m_isManagerRole = isManagerRole;
    emit isManagerRoleChanged();
}
