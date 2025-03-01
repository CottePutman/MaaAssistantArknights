#include "SSSStageManagerTask.h"

#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/Miscellaneous/SSSCopilotConfig.h"
#include "Controller.h"
#include "Task/ProcessTask.h"
#include "Task/SSS/SSSBattleProcessTask.h"
#include "Utils/Logger.hpp"
#include "Vision/OcrImageAnalyzer.h"
#include "Vision/OcrWithPreprocessImageAnalyzer.h"

bool asst::SSSStageManagerTask::_run()
{
    LogTraceFunction;

    preprocess_data();

    while (!need_exit()) {
        do {
        } while (!comfirm_battle_complete() && !need_exit()); //

        auto stage_opt = analyze_stage();
        if (!stage_opt) {
            Log.warn("unknown stage, run!");
            
            auto info = basic_info_with_what("SSSSettlement");
            info["why"] = "Recognition error or JSON does not support this.";
            callback(AsstMsg::SubTaskExtraInfo, info);

            settle();
            break;
        }

        const std::string& stage_name = stage_opt.value();
        int times = m_remaining_times[stage_name] + 1;

        SSSBattleProcessTask battle_task(m_callback, m_inst, m_task_chain);
        battle_task.set_stage_name(stage_name);

        bool success = false;
        for (int i = 0; i < times; ++i) {
            Log.info("try to fight", i);
            if (click_start_button() && battle_task.run() && !need_exit()) {
                success = true;
                break;
            }
        }
        if (!need_exit() && !success) {
            Log.warn("Can't win, run!");
            
            auto info = basic_info_with_what("SSSSettlement");
            info["why"] = "Can't win, run!";
            callback(AsstMsg::SubTaskExtraInfo, info);
            
            settle();
        }
    }
    return true;
}

void asst::SSSStageManagerTask::preprocess_data()
{
    for (const auto& [name, stage] : SSSCopilot.get_data().stages_data) {
        m_remaining_times[name] = stage.retry_times;
    }
}

std::optional<std::string> asst::SSSStageManagerTask::analyze_stage()
{
    OcrWithPreprocessImageAnalyzer analyzer(ctrler()->get_image());
    analyzer.set_task_info("SSSStageNameOCR");
    if (!analyzer.analyze()) {
        return std::nullopt;
    }

    analyzer.sort_result_by_score();
    const std::string& text = analyzer.get_result().front().text;

    const auto& stages_data = SSSCopilot.get_data().stages_data;
    for (const auto& [name, stage_info] : stages_data) {
        if (name == text) {
            auto info = basic_info_with_what("SSSStage");
            info["details"]["stage"] = name;
            callback(AsstMsg::SubTaskExtraInfo, info);
            return name;
        }
    }
    return std::nullopt;
}

bool asst::SSSStageManagerTask::comfirm_battle_complete()
{
    return ProcessTask(*this, { "SSSComfirmBattleComplete" })
        .set_times_limit("SSSStartFighting", 0)
        .set_times_limit("SSSBegin", 0)
        .run();
}

bool asst::SSSStageManagerTask::click_start_button()
{
    return ProcessTask(*this, { "SSSStartFighting", "SSSCloseTip" }).run();
}

bool asst::SSSStageManagerTask::settle()
{
    return ProcessTask(*this, { "SSSSettlement" }) //
               .run() &&
           ProcessTask(*this, { "SSSComfirmBattleComplete" })
               .set_times_limit("SSSStartFighting", 0)
               .set_times_limit("SSSBegin", 0)
               .run();
}
