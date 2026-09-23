// Copyright (C) 2026 Diderde
// SPDX-License-Identifier: MIT

//! 数据库服务探测：本机常见数据库端口占用（占用=服务在跑，属正常信息而非问题）。

use super::{CheckDef, CheckOut};
use crate::model::Config;
use std::net::{TcpStream, SocketAddr};
use std::time::Duration;

pub fn defs() -> Vec<CheckDef> {
    vec![CheckDef {
        id: "databases.ports",
        title: "本机数据库服务",
        category: "databases",
        platforms: &[],
        func: check_ports,
    }]
}

fn check_ports(_cfg: &Config) -> CheckOut {
    const PORTS: &[(u16, &str)] = &[
        (3306, "MySQL"),
        (5432, "PostgreSQL"),
        (6379, "Redis"),
        (27017, "MongoDB"),
        (1433, "SQL Server"),
        (1521, "Oracle"),
    ];
    let mut detail = Vec::new();
    for (port, name) in PORTS {
        let addr = SocketAddr::from(([127, 0, 0, 1], *port));
        if TcpStream::connect_timeout(&addr, Duration::from_millis(400)).is_ok() {
            detail.push(format!("端口 {port}：占用（可能是 {name}）"));
        }
    }
    if detail.is_empty() {
        CheckOut::info(vec!["未检测到本机监听的常见数据库端口".into()])
    } else {
        CheckOut::info(detail)
    }
}
