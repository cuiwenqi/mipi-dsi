%% --------------------------------
%% author:cuiwenqi
%% date: 20241024
%% function: The code of reference
%% note: 
%% reference: 
%% --------------------------------
clc;clear;close all;


% 示例数据  
data = uint16([0x10]);  
length = length(data);  
  
% 计算CRC  
crc = crc16(data, length);  
  
% 显示结果  
fprintf('CRC-16: %04X\n', crc);

function crc = crc16(data, length)
    % 定义多项式
    POLY = uint16(0x8408); % 1021H bit reversed
    
    % 初始化CRC值
    crc = uint16(0xFFFF);
    
    % 检查长度是否为0
    if length == 0
        return ;
    end
    
    % 遍历每个字节的数据
    for i = 1:length
        data_byte = uint16(data(i));
        
        % 处理每个字节的8位
        for j = 1:8
            if xor(bitand(crc, uint16(0x0001)) , bitand(data_byte, uint16(0x0001)))
                crc = bitxor(bitshift(crc, -1), POLY);
            else
                crc = bitshift(crc, -1);
            end
            data_byte = bitshift(data_byte, -1);
        end
    end
    
    % 返回最终的CRC值
   
%     crc = bitor(bitshift(bitand(crc, uint16(0x00ff)), 8) , ...  
%                   bitshift(bitand(crc, uint16(0xff00)), -8));
      crc = uint16(crc);
end

