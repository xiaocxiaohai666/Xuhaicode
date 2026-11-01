-- ============================================
-- chat_room_online 业务库建表脚本，项目运行前，先执行一遍。
-- 字符集统一 utf8mb4，支持中文与 emoji
-- 执行：mysql -uroot -p < docs/schema.sql
-- ============================================

CREATE DATABASE IF NOT EXISTS chat_room_db DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;
USE chat_room_db;

-- 用户表：id 自增且不复用，注销后直接删除记录
CREATE TABLE IF NOT EXISTS users(
    id INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(64) NOT NULL,
    passwd VARCHAR(128) NOT NULL,
    status TINYINT NOT NULL DEFAULT 0,
    created_at BIGINT NOT NULL DEFAULT 0,
    UNIQUE KEY uk_users_name(name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 好友表：好友关系存双向两行（a→b、b→a），查询时只按 user_id 过滤
CREATE TABLE IF NOT EXISTS friends(
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    user_id INT NOT NULL,
    friend_id INT NOT NULL,
    created_at BIGINT NOT NULL DEFAULT 0,
    UNIQUE KEY uk_friends_pair(user_id,friend_id),
    KEY idx_friends_user(user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 群表：群名唯一（groups 是 MySQL 8 保留字，必须加反引号）
CREATE TABLE IF NOT EXISTS `groups`(
    id INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(64) NOT NULL,
    `desc` VARCHAR(255) NOT NULL DEFAULT '',
    owner_id INT NOT NULL,
    created_at BIGINT NOT NULL DEFAULT 0,
    UNIQUE KEY uk_groups_name(name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 群成员表：群主也必须是成员
CREATE TABLE IF NOT EXISTS group_members(
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    group_id INT NOT NULL,
    user_id INT NOT NULL,
    joined_at BIGINT NOT NULL DEFAULT 0,
    UNIQUE KEY uk_group_members_pair(group_id,user_id),
    KEY idx_group_members_user(user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 聊天消息表：私聊 to_id 有效、group_id=0；群聊 group_id 有效、to_id=0
CREATE TABLE IF NOT EXISTS messages(
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    type TINYINT NOT NULL,
    from_id INT NOT NULL,
    to_id INT NOT NULL DEFAULT 0,
    group_id INT NOT NULL DEFAULT 0,
    content TEXT NOT NULL,
    created_at BIGINT NOT NULL DEFAULT 0,
    KEY idx_messages_to(to_id,id),
    KEY idx_messages_group(group_id,id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 离线消息表：Redis 判定无人订阅时入队，登录后投递并删除
CREATE TABLE IF NOT EXISTS offline_messages(
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    to_id INT NOT NULL,
    from_id INT NOT NULL,
    group_id INT NOT NULL DEFAULT 0,
    content TEXT NOT NULL,
    created_at BIGINT NOT NULL DEFAULT 0,
    KEY idx_offline_to(to_id,id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
