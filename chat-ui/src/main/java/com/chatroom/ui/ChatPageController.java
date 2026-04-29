package com.chatroom.ui;

import org.springframework.stereotype.Controller;
import org.springframework.ui.Model;
import org.springframework.web.bind.annotation.GetMapping;

@Controller
public class ChatPageController {

    @GetMapping("/")
    public String index(Model model) {
        model.addAttribute("title", "聊天室压测可视化监控台");
        return "index";
    }

    @GetMapping("/server")
    public String serverPage(Model model) {
        model.addAttribute("title", "聊天室服务端控制台");
        return "server";
    }

    @GetMapping("/client")
    public String clientPage(Model model) {
        model.addAttribute("title", "聊天室客户端控制台");
        return "client";
    }
}
