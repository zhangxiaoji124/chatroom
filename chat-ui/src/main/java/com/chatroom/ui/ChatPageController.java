package com.chatroom.ui;

import org.springframework.stereotype.Controller;
import org.springframework.ui.Model;
import org.springframework.web.bind.annotation.GetMapping;

@Controller
public class ChatPageController {

    @GetMapping("/")
    public String index(Model model) {
        model.addAttribute("title", "百万级数据库压测实时监控台");
        return "index";
    }

    @GetMapping("/server")
    public String serverPage(Model model) {
        model.addAttribute("title", "聊天室服务端控制台");
        return "server";
    }

    @GetMapping("/client")
    public String clientPage(Model model) {
        model.addAttribute("title", "聊天室");
        return "client";
    }
}
